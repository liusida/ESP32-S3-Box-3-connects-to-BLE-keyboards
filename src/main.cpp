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
static void subscribeAllInputReports(NimBLERemoteService* hid) {
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


/* --- Connect and subscribe (Report Protocol) --- */
static bool connectAndSubscribe(const NimBLEAdvertisedDevice* adv) {
  Serial.print("[BLE] Connecting to ");
  Serial.println(adv->getAddress().toString().c_str());

  g_client = NimBLEDevice::createClient();
  if (!g_client->connect(adv)) {
    Serial.println("[BLE] Connect failed");
    NimBLEDevice::deleteClient(g_client);
    g_client = nullptr;
    return false;
  }
  Serial.println("[BLE] Connected");

  NimBLERemoteService* hid = g_client->getService(UUID_HID_SERVICE);
  if (!hid) {
    Serial.println("[HID] HID service not found");
    g_client->disconnect();
    return false;
  }

  // We can stay in Report Protocol (default). If a device supports Boot Mode and you want it:
  // if (auto* proto = hid->getCharacteristic(UUID_PROTO_MODE)) { uint8_t boot=0x00; proto->writeValue(&boot,1,true); }

  // Optional: read Report Map for debugging
  if (auto* mapChr = hid->getCharacteristic(UUID_REPORT_MAP)) {
    std::string map = mapChr->readValue();
    Serial.print("[HID] Report Map size: ");
    Serial.println(map.size());
    // If you want, print some bytes:
    // for (size_t i=0;i<min<size_t>(map.size(),32);++i){ if((uint8_t)map[i]<16)Serial.print('0'); Serial.print((uint8_t)map[i],HEX); Serial.print(' '); } Serial.println();
  }

  subscribeAllInputReports(hid);
  return !g_inputs.empty();
}

/* --- Scan helpers (same as your working version) --- */
static const NimBLEAdvertisedDevice* pickKeyboardFrom(const NimBLEScanResults& res) {
  for (int i = 0; i < res.getCount(); ++i) {
    const NimBLEAdvertisedDevice* d = res.getDevice(i);
    if (!d) continue;
    bool hasHID = d->isAdvertisingService(UUID_HID_SERVICE);
    std::string name = d->getName();
    bool looksLikeKb = name.find("Keyboard") != std::string::npos;

    if (hasHID || looksLikeKb) {
      Serial.print("[SCAN] Candidate: ");
      Serial.print(name.c_str());
      Serial.print("  ");
      Serial.println(d->getAddress().toString().c_str());
      return d;
    }
  }
  return nullptr;
}

static void scanAndConnectLoop() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(45);
  scan->setWindow(30);

  while (true) {
    Serial.println("[SCAN] Scanning for 5s...");
    NimBLEScanResults results = scan->getResults(5000, /*continue*/ false);

    const NimBLEAdvertisedDevice* candidate = pickKeyboardFrom(results);
    if (candidate) {
      NimBLEAdvertisedDevice copy = *candidate; // copy before clearing results
      if (connectAndSubscribe(&copy)) return;   // success
      Serial.println("[BLE] Connect/subscribe failed; retrying scan...");
    } else {
      Serial.println("[SCAN] No keyboard found; rescanning...");
    }
    scan->clearResults();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== ESP32-S3 BLE Keyboard Host (Report Protocol) ===");

  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setSecurityAuth(false, false, true);

  scanAndConnectLoop();
}

void loop() {
  if (g_client && !g_client->isConnected()) {
    Serial.println("[BLE] Disconnected. Re-scanning...");
    NimBLEDevice::deleteClient(g_client);
    g_client = nullptr;
    g_inputs.clear();
    scanAndConnectLoop();
  }
  delay(200);
}
