# Профиль совместимости Android / Android compatibility profile

## Русский

Этот документ описывает требования приложения и ограничения Android без публикации серийных номеров, Bluetooth-адресов, идентификаторов сборки или инвентаризации личного телефона.

### Требования приложения

- Android 6.0 (API 23) или новее для текущей сборки.
- Bluetooth Classic с поддержкой RFCOMM/SPP.
- Наушники должны быть предварительно сопряжены в системных настройках.
- Разрешения Bluetooth выдаются приложению при первом использовании в соответствии с версией Android.
- Для регулировки громкости приложение меняет медиапоток телефона; системные ограничения громкости и политики производителя сохраняются.

### Аудио, уведомления и фон

Управление ANC и EQ использует отдельное SPP-соединение и не требует переключения аудиомаршрута. Воспроизведение звука остаётся задачей аудиоприложения и Bluetooth A2DP. Android и прошивки производителей могут ограничивать фоновую работу, отображение уведомлений и элементы управления на экране блокировки. Пользователь должен разрешить уведомления и медиаэлементы управления в системных настройках.

Работа в фоне зависит от политики энергосбережения производителя. Если соединение или уведомление прекращается, проверьте разрешение автозапуска/фоновой активности и исключение приложения из оптимизации батареи.

## English

This document describes app requirements and Android limitations without publishing serial numbers, Bluetooth addresses, build identifiers, or a personal phone inventory.

### App requirements

- Android 6.0 (API 23) or later for the current build.
- Bluetooth Classic with RFCOMM/SPP support.
- The headset must already be paired in system settings.
- Grant Bluetooth permissions on first use, as required by the Android version.
- Volume control changes the phone's media stream; system volume limits and vendor policies still apply.

### Audio, notifications, and background operation

ANC and EQ control use a separate SPP connection and do not require changing the audio route. Audio playback remains the responsibility of the media app over Bluetooth A2DP. Android and vendor firmware may restrict background activity, notifications, and lock-screen media controls. Enable notifications and media controls in system settings.

Background reliability depends on the vendor's power-management policy. If the connection or notification stops, review the app's autostart/background permissions and battery-optimization settings.
