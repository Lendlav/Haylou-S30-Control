# HAYLOU S30 Control for Android

A small native Android app for ANC, built-in EQ presets, and the phone's media volume. It uses only Android's framework APIs; it has no app libraries, ads, analytics, network access, or custom-EQ transfer. User EQ is omitted because HAYLOU S30 rejects that command. Built-in EQ presets use the production device-config command used by the official sound settings screen.

## HAYLOU S30 protocol notes

- ANC remains on command `0x08` with payload `[02 04 mode]`.
- A built-in EQ preset is device config ID `7`, sent with command `0xF2` and payload `[03 00 07 mode]`. Preset values are `0` through `8`.
- The older `0x08` payload `[02 02 mode]` is used by an internal test screen in the vendor APK, not its normal sound-preset screen. It can be acknowledged without changing the active EQ. The normal screen calls `BtDeviceEqSettingRequest`, which sends `0xF2` config `7`.
- Custom EQ transfer is intentionally absent: the headset rejects that operation.

## Features

- Connects to a previously paired HAYLOU S30 over its Bluetooth Classic SPP service (`0000FD3D-0000-1000-8000-00805F9B34FB`).
- Sends ANC and built-in EQ commands only when the user presses a control.
- Adjusts Android's current media stream volume. The phone's hardware volume buttons work too.
- Keeps a public MediaStyle notification and media session while connected. Android can show the controls on the lock screen; notification and lock-screen media controls must be enabled in Android settings. The lock-screen buttons switch ANC and step through built-in EQ presets.
- Shows the selected paired headset and automatically reconnects its SPP control channel when a paired HAYLOU S30 appears again.

The app cannot override the phone's lock-screen privacy settings or bypass device unlock. Some Android vendors hide notifications or media controls until they are enabled in system settings.

## Build

Run `./build.sh` with an Android SDK platform `android.jar` (API 23 or newer), `aapt`, `d8` or `dalvik-exchange`/`dx`, `zipalign`, `apksigner`, a Java compiler (`javac` or ECJ), a Java runtime, and `zip`. Set `ANDROID_HOME` or `ANDROID_JAR`; tool paths can be overridden with `AAPT`, `D8`/`DX`, `ZIPALIGN`, and `APKSIGNER`.

The script writes `build/HaylouS30Control.apk` and signs it with a local debug key for sideloading. The key is stored outside the project under `$XDG_DATA_HOME/haylou-s30-control/` (or `$HOME/.local/share/haylou-s30-control/`); set `HAYLOU_DEBUG_KEYSTORE` to use another path. Keep this key to build updates that install over the same APK. It is for personal builds, not public distribution.

## Install and use

1. Install the APK, then pair HAYLOU S30 in Android Bluetooth settings.
2. Open the app and allow nearby-device/Bluetooth access when Android requests it.
3. Select the paired headset and tap **Подключить**.
4. Choose an ANC mode or one of the nine built-in EQ presets. Use the volume slider or the phone's volume buttons for playback level.
5. Allow the notification and enable lock-screen notifications/media controls in Android settings if the controls are not visible while locked.

The app does not need an account, internet access, location, contacts, files, or microphone access.
