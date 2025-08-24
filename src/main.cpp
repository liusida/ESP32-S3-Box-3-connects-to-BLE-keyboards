#include "BleKeyboardHost.h"

BleKeyboardHost keyboard;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== BLE Keyboard Host ===");
  keyboard.begin();
  keyboard.connect();
}

void loop() {
  if (!keyboard.isReady()) {
    Serial.println("[BLE] Keyboard not ready. Reconnecting...");
    keyboard.connect();
    delay(5000);
  }
  keyboard.pollLogs();
  delay(200);
}
