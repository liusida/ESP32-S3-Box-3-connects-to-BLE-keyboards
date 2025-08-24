# Project Description

This repository contains a minimal example project demonstrating how to interface an ESP32-S3-Box-3 with a BLE keyboard using NimBLE-Arduino v2.3.4 in a PlatformIO environment.

The project shows how to:

- Establish a BLE connection

- Receive keystrokes from the keyboard (using the HID report protocol, not the boot protocol)

- Print them to the Serial Monitor

The goal is to provide a clear, lightweight reference, since many AI-generated examples tend to be unnecessarily complex or incomplete.

What you would see:

```
[HID] Report (len=8, handle=35): 00 00 04 00 00 00 00 00 'a'
[HID] Report (len=8, handle=35): 00 00 00 00 00 00 00 00
[HID] Report (len=8, handle=35): 00 00 05 00 00 00 00 00 'b'
[HID] Report (len=8, handle=35): 00 00 00 00 00 00 00 00
[HID] Report (len=8, handle=35): 00 00 06 00 00 00 00 00 'c'
[HID] Report (len=8, handle=35): 00 00 00 00 00 00 00 00
[HID] Report (len=8, handle=35): 00 00 07 00 00 00 00 00 'd'
[HID] Report (len=8, handle=35): 00 00 00 00 00 00 00 00
[HID] Report (len=8, handle=35): 80 00 00 00 00 00 00 00 [mod=0x80]
[HID] Report (len=8, handle=35): 00 00 00 00 00 00 00 00
[HID] Report (len=8, handle=35): 00 00 51 00 00 00 00 00 [0x51]
[HID] Report (len=8, handle=35): 00 00 00 00 00 00 00 00
```
