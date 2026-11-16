# HAYLOU S30 Control

Independent Android and Ubuntu applications for controlling HAYLOU S30 headphones, alongside documented Bluetooth protocol and firmware research.

[Русский](#русский) · [English](#english)

---

## Русский

### О проекте

Проект объединяет компактные приложения управления HAYLOU S30 и материалы исследования гарнитуры на платформе Jieli. Управляющие команды отправляются по Bluetooth Classic SPP; подключение к аудио для управления ANC и эквалайзером не требуется.

### Возможности

- **Ubuntu:** выбор наушников, подключение к управляющему SPP-сервису, переключение ANC и встроенных пресетов EQ, настройка громкости системного Bluetooth-аудиовыхода.
- **Android:** управление ANC и встроенными пресетами EQ, регулировка медиагромкости, автоматическое переподключение и элементы управления в уведомлении/на экране блокировки.
- **Исследование:** описание протокола, результатов проверки команд, сведений о железе и прошивке, ограничений и безопасных дальнейших шагов.

Пользовательский эквалайзер не реализован: гарнитура отклоняет проверенную команду произвольного EQ. Голосовое управление в приложениях удалено.

### Начало работы

Инструкции по зависимостям, сборке и установке:

- [Ubuntu](apps/linux/README.md)
- [Android](apps/android/README.md)

### Исследовательские материалы

- [Краткая сводка](docs/RESEARCH_SUMMARY.md)
- [Профиль устройства](docs/DEVICE_PROFILE.md)
- [Bluetooth SPP и команды](docs/PROTOCOL.md)
- [Прошивка и SDK Jieli](docs/FIRMWARE_RESEARCH.md)
- [Аппаратная часть и документы FCC](docs/HARDWARE_AND_FCC.md)
- [Безопасное руководство по дальнейшему исследованию](docs/SAFE_RESEARCH_GUIDE.md)
- [Источники](docs/SOURCES.md)

### Ключевые результаты

- Для управления используется Bluetooth Classic SPP UUID `0000FD3D-0000-1000-8000-00805F9B34FB`.
- ANC переключается командой `0x08` с атрибутом `4` и значениями `0–2`.
- Встроенные пресеты EQ переключаются командой `0xF2`, config ID `7`, значения `0–8`.
- Команда из внутреннего тестового экрана приложения производителя могла получить подтверждение, не меняя фактический пресет EQ.
- SDK Jieli содержит общие механизмы системных звуков, включая предупреждение о низком заряде. Их наличие не доказывает, что найденная реализация совпадает с прошивкой HAYLOU S30; готовый патч прошивки не найден.
- Доступный FCC-отчёт неполон: в нём отсутствует часть страниц и ожидаемые внутренние фотографии. Он не подтверждает расположение точек прошивки или разводку платы.

Подробности и степень уверенности приведены в связанных документах.

### Приватность и безопасность

Ключи подписи, APK, бинарные сборки, модели, SDK-библиотеки, дампы прошивки, Bluetooth-захваты и данные сопряжения не включаются в репозиторий. Сборки и локальные артефакты исключены через `.gitignore`.

Исследование прошивки остаётся документальным: никаких неподтверждённых операций записи прошивки проект не предлагает.

---

## English

### About

This project combines lightweight HAYLOU S30 control apps with research notes about the Jieli-based headset. Control commands use Bluetooth Classic SPP; ANC and EQ control do not require an audio connection.

### Features

- **Ubuntu:** select and connect to the headset's SPP control service, change ANC and built-in EQ presets, and adjust system Bluetooth output volume.
- **Android:** control ANC and built-in EQ presets, adjust media volume, reconnect automatically, and use notification or lock-screen controls.
- **Research:** documented protocol findings, command checks, hardware and firmware notes, known limitations, and safe next steps.

A custom equalizer is not provided because the headset rejects the tested custom-EQ command. Voice control has been removed from the apps.

### Getting started

See the platform guides for dependencies, build, and installation:

- [Ubuntu](apps/linux/README.md)
- [Android](apps/android/README.md)

### Research notes

- [Research summary](docs/RESEARCH_SUMMARY.md)
- [Device profile](docs/DEVICE_PROFILE.md)
- [Bluetooth SPP and commands](docs/PROTOCOL.md)
- [Jieli firmware and SDK](docs/FIRMWARE_RESEARCH.md)
- [Hardware and FCC documents](docs/HARDWARE_AND_FCC.md)
- [Safe research guide](docs/SAFE_RESEARCH_GUIDE.md)
- [Sources](docs/SOURCES.md)

### Key findings

- Bluetooth Classic SPP UUID `0000FD3D-0000-1000-8000-00805F9B34FB` is used for control.
- ANC uses command `0x08`, attribute `4`, and values `0–2`.
- Built-in EQ presets use command `0xF2`, config ID `7`, and values `0–8`.
- A command from an internal vendor-app test screen could be acknowledged without changing the active EQ preset.
- The Jieli SDK contains general system-tone mechanisms, including a low-battery warning. This does not establish that the implementation matches HAYLOU S30 firmware; no ready-to-use firmware patch has been found.
- The available FCC report is incomplete: some pages and expected internal photos are missing. It does not confirm programming pads or the PCB layout.

See the linked documents for details and confidence levels.

### Privacy and safety

Signing keys, APKs, build binaries, models, SDK libraries, firmware dumps, Bluetooth captures, and pairing data are excluded from the repository. Build output and local artifacts are excluded by `.gitignore`.

Firmware work remains documentary: this project does not recommend unverified firmware writes.
