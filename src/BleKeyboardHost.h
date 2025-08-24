#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>
#include <queue>

class BleKeyboardHost {
public:
    BleKeyboardHost();

    void begin();
    void connect(NimBLERemoteCharacteristic::notify_callback callback, uint32_t duration=500);
    bool isReady() const;
    void pollLogs();
    void pushLog(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool isNotify);

private:
    NimBLEClient *client;
    std::vector<NimBLERemoteCharacteristic*> inputs;
    std::queue<String> logQueue;

    static const NimBLEUUID UUID_HID_SERVICE;
    static const NimBLEUUID UUID_REPORT;

    static BleKeyboardHost *instance;  // singleton pointer

    void subscribeReports(NimBLERemoteCharacteristic::notify_callback callback);

    static char hidToAscii(uint8_t kc, bool shift);
};
