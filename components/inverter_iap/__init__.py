"""ESPHome external component: aktualizacja firmware falownika Anenji GM6200-48PL (IAP).

Realizuje protokół IAP (TMS320F28066 / rdzeń C28x) odtworzony z analizy
oryginalnego IAPTool.exe i deasemblacji bootloadera. Obraz firmware (.udt lub
.hex) jest deszyfrowany i osadzany we flash ESP32 na etapie kompilacji; wgrywanie
do falownika odbywa się po tym samym UART co Modbus (ruch Modbus jest wstrzymywany
na czas aktualizacji).

UWAGA: aktualizacja firmware może trwale uszkodzić falownik ("cegła"). Realny
zapis (kasowanie + programowanie) wykonywany jest TYLKO gdy allow_update: true.
Falownik powinien być w trybie standby, bez obciążenia i bez AC/PV.
"""

import base64
import subprocess

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, text_sensor, uart
from esphome.const import (
    CONF_ADDRESS,
    CONF_FLOW_CONTROL_PIN,
    CONF_ID,
    DEVICE_CLASS_EMPTY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
)
from esphome import pins
from esphome.core import CORE
from esphome.cpp_generator import RawExpression, RawStatement

CODEOWNERS = ["@witusio"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "text_sensor"]
MULTI_CONF = True

inverter_iap_ns = cg.esphome_ns.namespace("inverter_iap")
InverterIAP = inverter_iap_ns.class_("InverterIAP", cg.Component, uart.UARTDevice)

CONF_FIRMWARE = "firmware"
CONF_OBJECT = "object"
CONF_ALLOW_UPDATE = "allow_update"
CONF_DEBUG = "debug"
CONF_MODBUS_ID = "modbus_id"
CONF_PROGRESS = "progress"
CONF_STATUS = "status"
CONF_INVERTER_IAP_ID = "inverter_iap_id"

# --- stałe protokołu (profil TP2300 / GM6200-48PL) ---
AES_KEY = b"Ng4aEcEzjwtjKzwhqmynOjrbjJSaZxHJ"
AES_IV = b"wBVBT7aApgSI007x"
REGION_START = 0x3D8000
REGION_END = 0x3F3FFF
FLAG_ADDR = 0x3D8001
FLAG_VALUE = 0x2300
WORDS_PER_BLOCK = 128

OBJECTS = {"app": 0, "boot": 1}


# ---------------------------------------------------------------------------
# Deszyfrowanie / parsowanie obrazu firmware (port 1:1 z anenji_iap.py)
# ---------------------------------------------------------------------------
def _decrypt_udt(raw: bytes) -> bytes:
    data = base64.b64decode(raw)
    try:  # preferowany: moduł 'cryptography' (bez zewn. procesów)
        from cryptography.hazmat.primitives import padding
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

        dec = Cipher(algorithms.AES(AES_KEY), modes.CBC(AES_IV)).decryptor()
        out = dec.update(data) + dec.finalize()
        unp = padding.PKCS7(128).unpadder()
        return unp.update(out) + unp.finalize()
    except ImportError:
        pass
    # fallback: openssl
    proc = subprocess.run(
        ["openssl", "enc", "-d", "-aes-256-cbc", "-K", AES_KEY.hex(), "-iv", AES_IV.hex()],
        input=data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise cv.Invalid(
            "Nie udało się odszyfrować .udt (brak modułu 'cryptography' oraz błąd "
            "openssl): " + proc.stderr.decode("latin-1", "replace")
        )
    return proc.stdout


def _parse_intel_hex(text: str) -> dict:
    words: dict = {}
    base = 0
    for rec in text.split(":"):
        rec = rec.strip()
        if len(rec) < 10:
            continue
        try:
            b = bytes.fromhex(rec)
        except ValueError:
            continue
        n, addr, typ = b[0], (b[1] << 8) | b[2], b[3]
        if (sum(b) & 0xFF) != 0:  # suma rekordu HEX
            continue
        d = b[4 : 4 + n]
        if typ == 4:
            base = ((d[0] << 8) | d[1]) << 16
        elif typ == 0:
            a = base + addr
            for i in range(0, n, 2):
                words[a + i // 2] = (d[i] << 8) | d[i + 1]
        elif typ == 1:
            break
    return words


def _build_blocks(words: dict):
    bases = sorted(
        {(a & ~(WORDS_PER_BLOCK - 1)) for a in words if REGION_START <= a <= REGION_END}
    )
    blocks = []
    flash_sum = 0
    for base in bases:
        w = [words.get(base + i, 0xFFFF) for i in range(WORDS_PER_BLOCK)]
        flash_sum = (flash_sum + sum(w)) & 0xFFFFFFFF
        blocks.append((base, w))
    return blocks, flash_sum


def _is_url(value: str) -> bool:
    return value.startswith("http://") or value.startswith("https://")


def _load_firmware_bytes(raw: bytes, name: str):
    low = name.split("?", 1)[0].lower()  # odetnij ewentualny query string z URL
    if low.endswith(".hex"):
        text = raw.decode("latin-1")
    elif low.endswith(".udt"):
        text = _decrypt_udt(raw).decode("latin-1")
    else:
        raise cv.Invalid("firmware musi być plikiem/URL .udt lub .hex")
    words = _parse_intel_hex(text)
    if words.get(FLAG_ADDR) != FLAG_VALUE:
        got = words.get(FLAG_ADDR)
        raise cv.Invalid(
            "Sygnatura typu niezgodna pod 0x%06X: %s (oczekiwano 0x%04X) — to nie jest "
            "firmware dla tego urządzenia."
            % (FLAG_ADDR, ("0x%04X" % got) if got is not None else "brak", FLAG_VALUE)
        )
    blocks, flash_sum = _build_blocks(words)
    if not blocks:
        raise cv.Invalid("Obraz firmware nie zawiera danych w zakresie aplikacji.")
    return blocks, flash_sum


def _validate_firmware(value):
    value = cv.string(value)
    if _is_url(value):
        return value  # URL pobierany przy kompilacji (z cache ESPHome)
    path = CORE.relative_config_path(value)
    try:
        with open(path, "rb"):
            pass
    except OSError as err:
        raise cv.Invalid(f"Nie można otworzyć pliku firmware '{value}': {err}")
    return value


# ---------------------------------------------------------------------------
# Schemat konfiguracji
# ---------------------------------------------------------------------------
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(InverterIAP),
            cv.Optional(CONF_ADDRESS, default=1): cv.int_range(min=1, max=247),
            cv.Required(CONF_FIRMWARE): _validate_firmware,
            cv.Optional(CONF_OBJECT, default="app"): cv.enum(OBJECTS, lower=True),
            cv.Optional(CONF_ALLOW_UPDATE, default=False): cv.boolean,
            cv.Optional(CONF_DEBUG, default=False): cv.boolean,
            cv.Optional(CONF_MODBUS_ID): cv.use_id(cg.Component),
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_PROGRESS): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_EMPTY,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_object(config[CONF_OBJECT]))
    cg.add(var.set_allow_update(config[CONF_ALLOW_UPDATE]))
    cg.add(var.set_debug(config[CONF_DEBUG]))

    if CONF_MODBUS_ID in config:
        mb = await cg.get_variable(config[CONF_MODBUS_ID])
        cg.add(var.set_modbus(mb))
    if CONF_FLOW_CONTROL_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_FLOW_CONTROL_PIN])
        cg.add(var.set_flow_control_pin(pin))
    if CONF_PROGRESS in config:
        s = await sensor.new_sensor(config[CONF_PROGRESS])
        cg.add(var.set_progress_sensor(s))
    if CONF_STATUS in config:
        t = await text_sensor.new_text_sensor(config[CONF_STATUS])
        cg.add(var.set_status_text_sensor(t))

    # --- osadzenie obrazu firmware we flash ESP32 ---
    fw = config[CONF_FIRMWARE]
    if _is_url(fw):
        from esphome.external_files import compute_local_file_path, download_content

        cache = compute_local_file_path("inverter_iap", fw)
        raw = download_content(fw, cache)
        blocks, flash_sum = _load_firmware_bytes(raw, fw)
    else:
        path = CORE.relative_config_path(fw)
        with open(path, "rb") as f:
            raw = f.read()
        blocks, flash_sum = _load_firmware_bytes(raw, str(path))

    blob = bytearray()
    addrs = []
    for base, words in blocks:
        addrs.append(base)
        for x in words:  # słowo: starszy bajt, potem młodszy (format pakietu Program)
            blob.append((x >> 8) & 0xFF)
            blob.append(x & 0xFF)

    prefix = str(config[CONF_ID].id)
    data_name = prefix + "_fw_data"
    addr_name = prefix + "_fw_addr"
    cg.add_global(
        RawStatement(
            "static const uint8_t %s[] = {%s};"
            % (data_name, ",".join(str(b) for b in blob))
        )
    )
    cg.add_global(
        RawStatement(
            "static const uint32_t %s[] = {%s};"
            % (addr_name, ",".join(str(a) for a in addrs))
        )
    )
    cg.add(
        var.set_firmware(
            RawExpression(data_name),
            RawExpression(addr_name),
            len(blocks),
            flash_sum,
        )
    )
