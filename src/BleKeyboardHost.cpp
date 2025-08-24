#include "BleKeyboardHost.h"

/* --- Static members --- */
const NimBLEUUID BleKeyboardHost::UUID_HID_SERVICE((uint16_t)0x1812);
const NimBLEUUID BleKeyboardHost::UUID_REPORT((uint16_t)0x2A4D);
BleKeyboardHost* BleKeyboardHost::instance = nullptr;

/* --- Constructor --- */
BleKeyboardHost::BleKeyboardHost() : client(nullptr) {}

/* --- Public API --- */
void BleKeyboardHost::begin() {
    instance = this; // set global instance
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_N12);
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
}

void BleKeyboardHost::connect() {
    std::vector<NimBLEAddress> bonded;
    for (int i = 0; i < NimBLEDevice::getNumBonds(); i++)
        bonded.push_back(NimBLEDevice::getBondedAddress(i));

    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(45);
    scan->setWindow(45);

    Serial.println("[BLE] Scanning 500ms for HID device...");
    NimBLEScanResults res = scan->getResults(500, false);
    Serial.printf("[BLE] Found %d devices.\n", res.getCount());

    for (int i = 0; i < res.getCount(); i++) {
        const NimBLEAdvertisedDevice *dev = res.getDevice(i);
        if (!dev) continue;

        bool addrMatch = false;
        for (size_t j = 0; j < bonded.size(); j++) {
            if (dev->getAddress().equals(bonded[j])) { addrMatch = true; break; }
        }

        bool looksLikeKb = dev->isAdvertisingService(UUID_HID_SERVICE);
        if (!addrMatch && !looksLikeKb) continue;

        Serial.printf("[BLE] Trying %s (%s)\n",
                      dev->getAddress().toString().c_str(),
                      dev->getName().c_str());

        client = NimBLEDevice::createClient();
        client->setConnectTimeout(500);

        if (client && client->connect(dev)) {
            Serial.println("[BLE] Connected via scan match");
            subscribeReports();
        }
    }
    scan->clearResults();
}

bool BleKeyboardHost::isReady() const {
    return client && client->isConnected() && !inputs.empty();
}

void BleKeyboardHost::pollLogs() {
    while (!logQueue.empty()) {
        Serial.println(logQueue.front());
        logQueue.pop();
    }
}

/* --- Static callback trampoline --- */
void BleKeyboardHost::notifyThunk(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool isNotify) {
    if (instance) instance->onReport(c, data, len, isNotify);
}

/* --- Report handler --- */
void BleKeyboardHost::onReport(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool) {
    char buf[256];
    int pos = snprintf(buf, sizeof(buf), "[HID] Report (len=%d, handle=%u): ",
                       (int)len, c->getHandle());

    for (size_t i = 0; i < len; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X%s",
                        data[i], (i + 1 < len) ? " " : "");
    }

    if (len == 8) {
        uint8_t mod = data[0];
        bool shift = (mod & 0x22);
        for (int i = 2; i <= 7; i++) {
            uint8_t kc = data[i];
            if (!kc) continue;
            if (kc == 0x01) {
                pos += snprintf(buf + pos, sizeof(buf) - pos, " [ROLLOVER]");
                continue;
            }
            char ch = hidToAscii(kc, shift);
            if (ch) pos += snprintf(buf + pos, sizeof(buf) - pos, " '%c'", ch);
            else    pos += snprintf(buf + pos, sizeof(buf) - pos, " [0x%02X]", kc);
        }
        if (mod) pos += snprintf(buf + pos, sizeof(buf) - pos, " [mod=0x%02X]", mod);
    }
    else if (len == 3) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        " [Media/System: %02X %02X %02X]",
                        data[0], data[1], data[2]);
    }
    else {
        pos += snprintf(buf + pos, sizeof(buf) - pos, " [Unhandled report]");
    }

    if (logQueue.size() > 50) logQueue.pop();
    logQueue.push(String(buf));
}

/* --- Subscribe to reports --- */
void BleKeyboardHost::subscribeReports() {
    NimBLERemoteService *hid = client->getService(UUID_HID_SERVICE);
    if (!hid) { client->disconnect(); return; }

    inputs.clear();
    const std::vector<NimBLERemoteCharacteristic*> &chars = hid->getCharacteristics(true);

    for (size_t i = 0; i < chars.size(); i++) {
        NimBLERemoteCharacteristic *chr = chars[i];
        if (!chr) continue;
        if (chr->getUUID() != UUID_REPORT) continue;
        if (chr->canNotify()) {
            Serial.printf("[HID] Subscribing Input Report: handle=%u\n", chr->getHandle());
            if (chr->subscribe(true, notifyThunk)) inputs.push_back(chr);
        }
    }
    if (inputs.empty()) Serial.println("[HID] No subscribable Input Reports found.");
}

/* --- HID map helper --- */
char BleKeyboardHost::hidToAscii(uint8_t kc, bool shift) {
    if (kc >= 0x04 && kc <= 0x1D) return shift ? (char)toupper('a' + kc - 0x04) : 'a' + kc - 0x04;
    switch (kc) {
        case 0x1E: return shift ? '!' : '1'; case 0x1F: return shift ? '@' : '2';
        case 0x20: return shift ? '#' : '3'; case 0x21: return shift ? '$' : '4';
        case 0x22: return shift ? '%' : '5'; case 0x23: return shift ? '^' : '6';
        case 0x24: return shift ? '&' : '7'; case 0x25: return shift ? '*' : '8';
        case 0x26: return shift ? '(' : '9'; case 0x27: return shift ? ')' : '0';
        case 0x2C: return ' '; case 0x28: return '\n'; case 0x2A: return '\b'; case 0x2B: return '\t';
        case 0x2D: return shift ? '_' : '-'; case 0x2E: return shift ? '+' : '=';
        case 0x2F: return shift ? '{' : '['; case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\'; case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\''; case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ','; case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        default: return 0;
    }
}
