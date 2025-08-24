#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>
#include <queue>

class BleKeyboardHost {
public:
    BleKeyboardHost();

    void begin();
    void connect();
    bool isReady() const;
    void pollLogs();

private:
    NimBLEClient *client;
    std::vector<NimBLERemoteCharacteristic*> inputs;
    std::queue<String> logQueue;

    static const NimBLEUUID UUID_HID_SERVICE;
    static const NimBLEUUID UUID_REPORT;

    static BleKeyboardHost *instance;  // singleton pointer

    static void notifyThunk(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool isNotify);
    void onReport(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool isNotify);
    void subscribeReports();

    static char hidToAscii(uint8_t kc, bool shift);
};
