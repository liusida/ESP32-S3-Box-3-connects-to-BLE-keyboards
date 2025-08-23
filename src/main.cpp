#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>

/* --- BLE UUIDs (HID over GATT) --- */
static NimBLEUUID UUID_HID_SERVICE((uint16_t)0x1812);
static NimBLEUUID UUID_PROTO_MODE ((uint16_t)0x2A4E);
static NimBLEUUID UUID_REPORT     ((uint16_t)0x2A4D);
static NimBLEUUID UUID_REPORT_MAP ((uint16_t)0x2A4B);
static NimBLEUUID UUID_REPORT_REF ((uint16_t)0x2908); // descriptor

/* --- Globals --- */
static NimBLEClient* g_client = nullptr;

/* We'll subscribe to all Input Report chars we find and keep their IDs for logs */
struct InputReportSub {
  NimBLERemoteCharacteristic* chr = nullptr;
  uint8_t reportId = 0; // 0 if no report ID used
};
static std::vector<InputReportSub> g_inputs;

/* --- Simple USB HID usage to ASCII map (subset) --- */
static char hidToAscii(uint8_t keycode, bool shift) {
  if (keycode >= 0x04 && keycode <= 0x1D) {
    char c = 'a' + (keycode - 0x04);
    return shift ? (char)toupper(c) : c;
  }
  switch (keycode) {
    case 0x1E: return shift ? '!' : '1';
    case 0x1F: return shift ? '@' : '2';
    case 0x20: return shift ? '#' : '3';
    case 0x21: return shift ? '$' : '4';
    case 0x22: return shift ? '%' : '5';
    case 0x23: return shift ? '^' : '6';
    case 0x24: return shift ? '&' : '7';
    case 0x25: return shift ? '*' : '8';
    case 0x26: return shift ? '(' : '9';
    case 0x27: return shift ? ')' : '0';
    case 0x2C: return ' ';
    case 0x28: return '\n';
    case 0x2A: return '\b';
    case 0x2B: return '\t';
    case 0x2D: return shift ? '_' : '-';
    case 0x2E: return shift ? '+' : '=';
    case 0x2F: return shift ? '{' : '[';
    case 0x30: return shift ? '}' : ']';
    case 0x31: return shift ? '|' : '\\';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x35: return shift ? '~' : '`';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    default:   return 0;
  }
}

static void printHex(const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (d[i] < 16) Serial.print('0');
    Serial.print(d[i], HEX);
    if (i + 1 < n) Serial.print(' ');
  }
}

/* --- Notification handler for Report Protocol (handles 8 or 9+ bytes) --- */
static void onReportNotify(NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool) {
  uint8_t repId = 0;
  size_t  off   = 0;

  if (len == 0) return;

  // If this characteristic has a Report ID, first byte is the ID
  // We can't reliably know from GATT only; so: if multiple Input Reports exist,
  // most devices use a Report ID. We'll infer:
  if (len >= 9) { // common: [reportId][8 bytes keyboard] ...
    repId = data[0];
    off   = 1;
  }

  Serial.print("[HID] Report (");
  Serial.print(len);
  Serial.print(") repId=");
  Serial.print(repId);
  Serial.print(": ");
  printHex(data, len);
  Serial.println();

  // Try to parse like a keyboard report when we have at least 8 bytes following the offset
  if (len >= off + 8) {
    uint8_t mod   = data[off + 0];
    bool shift    = (mod & 0x22) || (mod & 0x02) || (mod & 0x20); // left/right shift bits
    // bytes off+2..off+7 are up to 6 keycodes
    for (int i = 2; i <= 7; ++i) {
      uint8_t kc = data[off + i];
      if (kc == 0x00) continue;
      if (kc == 0x01) { Serial.println("[HID] Rollover"); continue; }
      char ch = hidToAscii(kc, shift);
      if (ch) Serial.print(ch);
      else {
        Serial.print("\n[HID] keycode: 0x");
        Serial.println(kc, HEX);
      }
    }
  }
}

/* Helper: read Report Reference descriptor (0x2908) → (reportId, reportType) */
static bool readReportRef(NimBLERemoteCharacteristic* chr, uint8_t& idOut, uint8_t& typeOut) {
  NimBLERemoteDescriptor* rr = chr->getDescriptor(UUID_REPORT_REF);
  if (!rr) return false;
  std::string v = rr->readValue();   // usually 2 bytes: [ReportID][ReportType]
  if (v.size() < 2) return false;
  idOut   = (uint8_t)v[0];
  typeOut = (uint8_t)v[1]; // 1=input, 2=output, 3=feature
  return true;
}

/* Subscribe to all Input Report characteristics (0x2A4D with type==Input) */
static void subscribeAllInputReportsFromHIDService() {
  NimBLERemoteService* hid = g_client->getService(UUID_HID_SERVICE);
  if (!hid) { g_client->disconnect(); return; }

  g_inputs.clear();

  const std::vector<NimBLERemoteCharacteristic*>& chars = hid->getCharacteristics(/*refresh=*/true);

  for (auto* chr : chars) {
    if (!chr) continue;
    if (chr->getUUID() != UUID_REPORT) continue;

    uint8_t repId = 0, repType = 0;
    if (!readReportRef(chr, repId, repType)) {
      repType = 1; // Assume input if no reference provided
    }

    if (repType == 1 && chr->canNotify()) {
      Serial.print("[HID] Subscribing Input Report: handle=");
      Serial.print(chr->getHandle());
      Serial.print(" reportId=");
      Serial.println(repId);

      if (chr->subscribe(true, onReportNotify)) {
        // Create and push back object explicitly
        InputReportSub entry;
        entry.chr = chr;
        entry.reportId = repId;
        g_inputs.push_back(entry);
      } else {
        Serial.println("[HID] Subscribe failed on an Input Report.");
      }
    }
  }

  if (g_inputs.empty()) {
    Serial.println("[HID] No subscribable Input Reports found.");
  }
}

static void connectKeyboard() {
  // Gather bonded addresses if you have them (NimBLE 2.3.4 has index-based getters)
  std::vector<NimBLEAddress> bondedAddrs;
  
  const int n = NimBLEDevice::getNumBonds();
  for (int i = 0; i < n; ++i) bondedAddrs.push_back(NimBLEDevice::getBondedAddress(i));

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(45);
  scan->setWindow(45);

  // Short scans in a loop so you can catch brief advertising bursts
  for (int attempt = 0; attempt < 1; ++attempt) {
    Serial.println("[BLE] Scanning 500ms for BLE HID device...");
    NimBLEScanResults res = scan->getResults(500, /*is_continue=*/false);
    Serial.printf("[BLE] Found %d devices.\n", res.getCount());

    for (int i = 0; i < res.getCount(); ++i) {
      const NimBLEAdvertisedDevice* dev = res.getDevice(i);
      if (!dev) continue;

      // Option 1: match exact address (works if not using RPA)
      bool addrMatches = false;
      for (auto& a : bondedAddrs) {
        if (dev->getAddress().equals(a)) { addrMatches = true; break; }
      }

      // Option 2: also accept “looks like our keyboard” as a fallback
      bool looksLikeKb = dev->isAdvertisingService(UUID_HID_SERVICE);

      if (addrMatches || looksLikeKb) {
        if (addrMatches) {
          Serial.println("[BLE] Bonded device.");
        }
        if (looksLikeKb) {
          Serial.println("[BLE] Any keyboard device.");
        }
        Serial.print("[BLE] Trying to connect to ");
        Serial.print(dev->getAddress().toString().c_str());
        Serial.print(" -- ");
        Serial.println(dev->getName().c_str());

        g_client = NimBLEDevice::createClient();
        g_client->setConnectTimeout(500);
        if (g_client && g_client->connect(dev)) {  // note: connect via *advertised device*
          Serial.println("[BLE] Connected via scan match");
          subscribeAllInputReportsFromHIDService();
        }
      }
    }

    scan->clearResults();
  }
}

void initBLEHost() {
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_N12);
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("\n=== ESP32-S3 BLE Keyboard Host (Report Protocol) ===");

  initBLEHost();

  connectKeyboard();
}
bool isKeyboardReady() {
  return g_client && g_client->isConnected() && !g_inputs.empty();
}
void loop() {
  if (!isKeyboardReady()) {
    Serial.println("[BLE] Keyboard is not ready. Re-scanning...");
    NimBLEDevice::deleteClient(g_client);
    g_client = nullptr;
    g_inputs.clear();
    connectKeyboard();
    delay(5000);
  }
  delay(200);
}
