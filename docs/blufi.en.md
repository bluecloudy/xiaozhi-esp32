# BluFi Provisioning (Integrated with esp-wifi-connect)

This document explains how to enable and use BluFi (BLE Wi-Fi provisioning) in XiaoZhi firmware, together with the built-in `esp-wifi-connect` component to complete Wi-Fi connection and credential storage. For the official BluFi protocol details, see the [Espressif documentation](https://docs.espressif.com/projects/esp-idf/zh_CN/stable/esp32/api-guides/ble/blufi.html).

## Prerequisites

- A chip and firmware configuration that support BLE are required.
- In `idf.py menuconfig`, enable `WiFi Configuration Method -> Esp Blufi` (`CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING=y`). If you want to use BluFi, you must disable the Hotspot option in the same menu; otherwise, Hotspot provisioning is used by default.
- Keep the default NVS and event loop initialization (already handled by the project's `app_main`).
- `CONFIG_BT_BLUEDROID_ENABLED` and `CONFIG_BT_NIMBLE_ENABLED` are mutually exclusive; only one can be enabled.

## Workflow

1) On the mobile side, connect to the device via BluFi (for example, the official EspBlufi app or a custom client), send Wi-Fi SSID/password, and optionally retrieve the Wi-Fi list scanned by the device through the BluFi protocol.
2) On the device side, in `ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP`, write credentials to `SsidManager` (stored in NVS, part of the `esp-wifi-connect` component).
3) Then start `WifiStation` to scan and connect; status is reported back via BluFi.
4) After successful provisioning, the device automatically connects to the new Wi-Fi; on failure, it returns a failure status.

## Usage Steps

1. Configuration: Enable `Esp Blufi` in menuconfig. Build and flash firmware.
2. Trigger provisioning: On first boot, if there is no saved Wi-Fi, the device enters provisioning mode automatically.
3. Mobile operation: Open EspBlufi App (or another BluFi client), search and connect to the device, choose whether to enable encryption, then enter and send Wi-Fi SSID/password.
4. Check result:
    - Success: BluFi reports successful connection, and the device connects to Wi-Fi automatically.
    - Failure: BluFi returns a failure status; you can resend credentials or check the router.

## Notes

- BluFi provisioning cannot be enabled at the same time as hotspot provisioning. If hotspot provisioning has started, hotspot mode is used by default. Keep only one provisioning method enabled in menuconfig.
- For repeated tests, clear or overwrite stored SSID entries (the `wifi` namespace) to avoid interference from old configurations.
- If you use a custom BluFi client, follow the official protocol frame format; see the official document link above.
- The official documentation already provides the EspBlufi app download link.
- Due to changes in the BluFi API in IDF 5.5.2, the Bluetooth name is `Xiaozhi-Blufi` when built with 5.5.2, and `BLUFI_DEVICE` in 5.5.1.
