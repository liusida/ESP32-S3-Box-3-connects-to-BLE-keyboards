#include "BleKeyboardHost.h"

BleKeyboardHost keyboard;

/* --- Static callback trampoline --- */
static void keyboardCallback(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool isNotify) {
  // inject your logic here instead of simply push logs.
  keyboard.pushLog(c, data, len, isNotify);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== BLE Keyboard Host ===");
  keyboard.begin();
  keyboard.connect(keyboardCallback, /*duration=*/500);
}

void loop() {
  if (!keyboard.isReady()) {
    Serial.println("[BLE] Keyboard not ready. Reconnecting...");
    keyboard.connect(keyboardCallback, /*duration=*/500);
    delay(5000);
  }
  
  // remove this if you don't push logs.
  keyboard.pollLogs();

  delay(200);
}
