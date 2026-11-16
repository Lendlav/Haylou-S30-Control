#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QByteArray>
#include <QColor>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QIcon>
#include <QLockFile>
#include <QDir>
#include <QMenu>
#include <QPalette>
#include <QPainter>
#include <QPaintEvent>
#include <QSettings>
#include <QGraphicsDropShadowEffect>
#include <QMainWindow>
#include <QMouseEvent>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStandardPaths>
#include <QSlider>
#include <QStatusBar>
#include <QThreadPool>
#include <QToolButton>
#include <QWidgetAction>
#include <QVBoxLayout>
#include <QVector>
#include <QRunnable>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QWindow>
#include <QStyleOptionSlider>

#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <cerrno>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <atomic>
#include <cstring>

namespace {

constexpr uint16_t kHaylouS30ServiceUuid = 0xFD3D;
constexpr char kStart[] = {char(0xAA), char(0xBB), char(0xCC)};
constexpr char kEnd[] = {char(0xDD), char(0xEE), char(0xFF)};

struct ThemePalette { const char *accent; const char *bright; const char *glow; const char *bg; const char *panel; const char *edge; };
constexpr ThemePalette kThemes[] = {
    {"#FFB300", "#FFC247", "#9A6810", "#080704", "#17130A", "#59400D"},
    {"#D7C54A", "#FFED8A", "#81722B", "#090900", "#191707", "#5B5420"},
    {"#75C4C0", "#B4EEEA", "#376E70", "#050A0A", "#121A19", "#315654"},
    {"#D68A4D", "#FFC28A", "#794727", "#0A0705", "#1C130D", "#62412B"},
    {"#8FBD32", "#D6F27A", "#526F1C", "#101214", "#252A2D", "#596368"}
};

std::atomic_bool gEnglish{false};

QString L(const char *ru, const char *en)
{
    return QString::fromUtf8(gEnglish.load() ? en : ru);
}

std::runtime_error makeError(const QString &message)
{
    return std::runtime_error(message.toUtf8().constData());
}

int resolveRfcommChannel(const bdaddr_t &remoteAddress)
{
    bdaddr_t localAddress{};
    sdp_session_t *session = sdp_connect(&localAddress, &remoteAddress, SDP_RETRY_IF_BUSY);
    if (!session)
        throw makeError(L("Не удалось запросить SDP у наушников: %1", "Could not query headphone SDP: %1")
                            .arg(QString::fromLocal8Bit(strerror(errno))));

    uuid_t serviceUuid{};
    sdp_uuid16_create(&serviceUuid, kHaylouS30ServiceUuid);
    uint32_t attributeRange = 0x0000ffff;
    sdp_list_t *searchList = sdp_list_append(nullptr, &serviceUuid);
    sdp_list_t *attributeList = sdp_list_append(nullptr, &attributeRange);
    sdp_list_t *records = nullptr;

    auto releaseQuery = [&] {
        if (records) {
            for (sdp_list_t *item = records; item; item = item->next)
                sdp_record_free(static_cast<sdp_record_t *>(item->data));
            sdp_list_free(records, nullptr);
            records = nullptr;
        }
        sdp_list_free(searchList, nullptr);
        searchList = nullptr;
        sdp_list_free(attributeList, nullptr);
        attributeList = nullptr;
        sdp_close(session);
        session = nullptr;
    };

    try {
        if (!searchList || !attributeList)
            throw makeError(L("Не удалось выделить данные для SDP-запроса", "Could not allocate SDP query data"));

        if (sdp_service_search_attr_req(session, searchList, SDP_ATTR_REQ_RANGE,
                                        attributeList, &records) < 0) {
            const QString reason = QString::fromLocal8Bit(strerror(errno));
            throw makeError(L("SDP-поиск UUID FD3D завершился ошибкой: %1", "SDP search for UUID FD3D failed: %1").arg(reason));
        }

        int channel = -1;
        for (sdp_list_t *item = records; item && channel < 0; item = item->next) {
            auto *record = static_cast<sdp_record_t *>(item->data);
            sdp_list_t *protocols = nullptr;
            const int protocolStatus = sdp_get_access_protos(record, &protocols);
            if (protocols) {
                if (protocolStatus == 0)
                    channel = sdp_get_proto_port(protocols, RFCOMM_UUID);
                for (sdp_list_t *protocol = protocols; protocol; protocol = protocol->next)
                    sdp_list_free(static_cast<sdp_list_t *>(protocol->data), nullptr);
                sdp_list_free(protocols, nullptr);
            }
        }

        releaseQuery();
        if (channel <= 0)
            throw makeError(L("В SDP не найден RFCOMM-канал службы HAYLOU S30 UUID FD3D. Убедитесь, что наушники сопряжены и доступны.",
                              "No RFCOMM channel for HAYLOU S30 service UUID FD3D was found in SDP. Make sure the headphones are paired and available."));
        return channel;
    } catch (...) {
        if (session)
            releaseQuery();
        throw;
    }
}

QString runProcess(const QString &program, const QStringList &arguments, int timeoutMs = 5000)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(2500))
        throw makeError(L("Не удалось запустить %1: %2", "Could not start %1: %2").arg(program, process.errorString()));
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        throw makeError(L("Превышено время ожидания %1", "Timed out waiting for %1").arg(program));
    }
    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error.isEmpty())
            error = output.trimmed();
        throw makeError(L("%1: %2", "%1: %2").arg(program, error.isEmpty() ? L("ошибка", "error") : error));
    }
    return output;
}

struct BluetoothDevice {
    QString address;
    QString name;
};

struct AudioSink {
    QString name;
    QString description;
    int volume = 0;
};

struct SystemLists {
    QVector<BluetoothDevice> devices;
    QVector<AudioSink> sinks;
    QString deviceError;
    QString sinkError;
};

QVector<BluetoothDevice> findS30Devices()
{
    QVector<BluetoothDevice> devices;
    const QString output = runProcess(QStringLiteral("bluetoothctl"), {QStringLiteral("devices")});
    const QRegularExpression deviceLine(QStringLiteral(R"(^Device\s+([0-9A-Fa-f:]{17})\s+(.+)$)"));
    const QRegularExpression aliasLine(QStringLiteral(R"(^\s*Alias:\s*(.+)$)"), QRegularExpression::MultilineOption);

    for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const auto match = deviceLine.match(line.trimmed());
        if (!match.hasMatch())
            continue;

        const QString address = match.captured(1);
        const QString listedName = match.captured(2).trimmed();
        const QString info = runProcess(QStringLiteral("bluetoothctl"), {QStringLiteral("info"), address});
        if (!info.contains(QStringLiteral("Paired: yes")))
            continue;
        const auto aliasMatch = aliasLine.match(info);
        const QString name = aliasMatch.hasMatch() ? aliasMatch.captured(1).trimmed() : listedName;
        const QString normalized = name.toCaseFolded();
        if (normalized.contains(QStringLiteral("haylou")) && normalized.contains(QStringLiteral("s30")))
            devices.push_back({address, name});
    }
    return devices;
}

QVector<AudioSink> findS30AudioSinks()
{
    const QString output = runProcess(QStringLiteral("pactl"), {QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"), QStringLiteral("sinks")});
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray())
        throw makeError(L("Не удалось прочитать список аудиовыходов PipeWire", "Could not read PipeWire audio outputs"));

    QVector<AudioSink> sinks;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        const QJsonObject properties = object.value(QStringLiteral("properties")).toObject();
        const QString description = object.value(QStringLiteral("description")).toString();
        const QString propertyDescription = properties.value(QStringLiteral("device.description")).toString();
        const QString combined = (description + QLatin1Char(' ') + propertyDescription).toCaseFolded();
        if (properties.value(QStringLiteral("device.bus")).toString() != QStringLiteral("bluetooth")
            || !combined.contains(QStringLiteral("haylou")) || !combined.contains(QStringLiteral("s30")))
            continue;

        int volume = 0;
        const QJsonObject channels = object.value(QStringLiteral("volume")).toObject();
        if (!channels.isEmpty()) {
            const QJsonObject firstChannel = channels.constBegin().value().toObject();
            QString percent = firstChannel.value(QStringLiteral("value_percent")).toString();
            percent.remove(QLatin1Char('%'));
            volume = percent.toDouble();
        }
        sinks.push_back({object.value(QStringLiteral("name")).toString(),
                         description.isEmpty() ? propertyDescription : description, volume});
    }
    return sinks;
}


class SppClient {
public:
    ~SppClient() { disconnectDevice(); }

    void setAncUpdateCallback(std::function<void(int)> callback)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        ancUpdateCallback_ = std::move(callback);
    }

    QString connectDevice(const QString &address)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        closeSocket();

        const QStringList pieces = address.split(QLatin1Char(':'));
        if (pieces.size() != 6)
            throw makeError(L("Некорректный Bluetooth-адрес", "Invalid Bluetooth address"));

        bdaddr_t remoteAddress{};
        for (int index = 0; index < 6; ++index) {
            bool ok = false;
            const uint byte = pieces.at(index).toUInt(&ok, 16);
            if (!ok || byte > 255)
                throw makeError(L("Некорректный Bluetooth-адрес", "Invalid Bluetooth address"));
            remoteAddress.b[5 - index] = static_cast<uint8_t>(byte);
        }

        const int rfcommChannel = resolveRfcommChannel(remoteAddress);

        const int fd = ::socket(AF_BLUETOOTH, SOCK_STREAM | SOCK_CLOEXEC, BTPROTO_RFCOMM);
        if (fd < 0)
            throw makeError(L("Не удалось создать RFCOMM-сокет: %1", "Could not create RFCOMM socket: %1").arg(QString::fromLocal8Bit(strerror(errno))));

        sockaddr_rc remote{};
        remote.rc_family = AF_BLUETOOTH;
        remote.rc_channel = static_cast<uint8_t>(rfcommChannel);
        remote.rc_bdaddr = remoteAddress;

        const int originalFlags = fcntl(fd, F_GETFL, 0);
        if (originalFlags < 0 || fcntl(fd, F_SETFL, originalFlags | O_NONBLOCK) < 0) {
            const QString message = L("Ошибка настройки RFCOMM-сокета: %1", "Could not configure RFCOMM socket: %1").arg(QString::fromLocal8Bit(strerror(errno)));
            ::close(fd);
            throw makeError(message);
        }

        int result = ::connect(fd, reinterpret_cast<sockaddr *>(&remote), sizeof(remote));
        if (result < 0 && errno != EINPROGRESS) {
            const QString message = L("Не удалось подключиться по SPP: %1", "SPP connection failed: %1").arg(QString::fromLocal8Bit(strerror(errno)));
            ::close(fd);
            throw makeError(message);
        }
        if (result < 0) {
            pollfd descriptor{fd, POLLOUT, 0};
            do { result = ::poll(&descriptor, 1, 10000); } while (result < 0 && errno == EINTR);
            if (result <= 0) {
                ::close(fd);
                throw makeError(result == 0 ? L("Время подключения к SPP истекло", "SPP connection timed out")
                                           : L("Ошибка ожидания SPP-соединения", "Error while waiting for SPP connection"));
            }
            int socketError = 0;
            socklen_t errorSize = sizeof(socketError);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &errorSize) < 0 || socketError != 0) {
                const int code = socketError != 0 ? socketError : errno;
                const QString message = L("Не удалось подключиться по SPP: %1", "SPP connection failed: %1").arg(QString::fromLocal8Bit(strerror(code)));
                ::close(fd);
                throw makeError(message);
            }
        }
        if (fcntl(fd, F_SETFL, originalFlags) < 0) {
            const QString message = L("Ошибка настройки RFCOMM-сокета: %1", "Could not configure RFCOMM socket: %1").arg(QString::fromLocal8Bit(strerror(errno)));
            ::close(fd);
            throw makeError(message);
        }
        socket_ = fd;
        sequence_ = 0;
        return L("SPP подключён к HAYLOU S30 (UUID FD3D, RFCOMM %1)", "SPP connected to HAYLOU S30 (UUID FD3D, RFCOMM %1)").arg(rfcommChannel);
    }

    QString disconnectDevice()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        closeSocket();
        return L("SPP отключён", "SPP disconnected");
    }

    bool pollEvents()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (socket_ < 0)
            return false;
        pollfd descriptor{socket_, static_cast<short>(POLLIN | POLLERR | POLLHUP), 0};
        int ready;
        do { ready = ::poll(&descriptor, 1, 0); } while (ready < 0 && errno == EINTR);
        if (ready < 0 || (ready > 0 && (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))
                          && !(descriptor.revents & POLLIN))) {
            closeSocket();
            return false;
        }
        if (ready <= 0 || !(descriptor.revents & POLLIN))
            return true;
        char chunk[2048];
        const ssize_t received = ::recv(socket_, chunk, sizeof(chunk), MSG_DONTWAIT);
        if (received == 0) {
            closeSocket();
            return false;
        }
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                return true;
            closeSocket();
            return false;
        }
        eventBuffer_.append(chunk, static_cast<qsizetype>(received));
        const QByteArray start(kStart, 3), end(kEnd, 3);
        while (true) {
            const int startAt = eventBuffer_.indexOf(start);
            if (startAt < 0) {
                eventBuffer_ = eventBuffer_.right(std::min<qsizetype>(2, eventBuffer_.size()));
                break;
            }
            if (startAt > 0)
                eventBuffer_.remove(0, startAt);
            const int endAt = eventBuffer_.indexOf(end, 8);
            if (endAt < 0)
                break;
            const QByteArray frame = eventBuffer_.left(endAt + end.size());
            eventBuffer_.remove(0, frame.size());
            consumeAncEvent(frame);
        }
        if (eventBuffer_.size() > 65536)
            eventBuffer_ = eventBuffer_.right(8192);
        if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            closeSocket();
            return false;
        }
        return true;
    }

    QString setAnc(int mode)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        QByteArray payload;
        payload.append(char(2)); // attribute data length
        payload.append(char(4)); // HAYLOU ANC mode attribute
        payload.append(static_cast<char>(mode));
        sendCommand(0x08, payload);
        return L("Ответ на команду ANC получен", "ANC command acknowledged");
    }

    QString setPreset(int preset)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        setPresetLocked(preset);
        return L("Ответ на команду EQ получен", "EQ command acknowledged");
    }


private:
    void closeSocket()
    {
        if (socket_ >= 0) {
            ::shutdown(socket_, SHUT_RDWR);
            ::close(socket_);
            socket_ = -1;
        }
    }

    void consumeAncEvent(const QByteArray &frame)
    {
        if (frame.size() < 18 || static_cast<uint8_t>(frame.at(3)) != 0xC0
            || static_cast<uint8_t>(frame.at(4)) != 0x0E)
            return;
        const int payloadEnd = frame.size() - 3;
        for (int i = 8; i + 2 < payloadEnd;) {
            const int fieldLength = static_cast<uint8_t>(frame.at(i));
            if (fieldLength < 1 || i + fieldLength >= payloadEnd)
                break;
            const int fieldId = static_cast<uint8_t>(frame.at(i + 1));
            if (fieldId == 0x08 && fieldLength == 2) {
                const int mode = static_cast<uint8_t>(frame.at(i + 2));
                if (mode <= 2 && ancUpdateCallback_)
                    ancUpdateCallback_(mode);
            }
            i += fieldLength + 1;
        }
    }

    void setPresetLocked(int preset)
    {
        QByteArray payload;
        payload.append(char(3)); // config data length + 2-byte config ID
        payload.append(char(0));
        payload.append(char(7)); // HOP_CONFIG_EQ_MODE from the production client
        payload.append(static_cast<char>(preset));
        sendCommand(0xF2, payload); // SET_DEVICE_CONFIG, used by the regular EQ screen
    }

    void sendAll(const QByteArray &bytes)
    {
        qsizetype offset = 0;
        while (offset < bytes.size()) {
            const ssize_t sent = ::send(socket_, bytes.constData() + offset,
                                        static_cast<size_t>(bytes.size() - offset), MSG_NOSIGNAL);
            if (sent > 0) {
                offset += sent;
                continue;
            }
            if (sent < 0 && errno == EINTR)
                continue;
            throw makeError(L("Ошибка отправки SPP-пакета: %1", "Failed to send SPP packet: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        }
    }

    QByteArray readResponse(int commandId, uint8_t sequence)
    {
        QByteArray buffer;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 4000) {
            const QByteArray start(kStart, 3);
            const QByteArray end(kEnd, 3);
            int startAt = buffer.indexOf(start);
            if (startAt < 0) {
                if (buffer.size() > 2)
                    buffer = buffer.right(2);
            } else {
                if (startAt > 0)
                    buffer.remove(0, startAt);
                const int endAt = buffer.indexOf(end, 8);
                if (endAt >= 0) {
                    const int frameLength = endAt + end.size();
                    const QByteArray frame = buffer.left(frameLength);
                    buffer.remove(0, frameLength);
                    if (frame.size() >= 12
                        && static_cast<uint8_t>(frame.at(3)) == 0x00
                        && static_cast<uint8_t>(frame.at(4)) == commandId
                        && static_cast<uint8_t>(frame.at(8)) == sequence) {
                        const auto status = static_cast<uint8_t>(frame.at(7));
                        if (status != 0)
                            throw makeError(L("Наушники отклонили команду (код %1)", "Headphones rejected the command (code %1)").arg(status));
                        return frame;
                    }
                    // Device-initiated status frames can arrive during a command too.
                    consumeAncEvent(frame);
                    continue;
                }
            }

            const int remaining = std::max(1, 4000 - static_cast<int>(timer.elapsed()));
            pollfd descriptor{socket_, POLLIN, 0};
            int ready;
            do { ready = ::poll(&descriptor, 1, remaining); } while (ready < 0 && errno == EINTR);
            if (ready == 0)
                break;
            if (ready < 0)
                throw makeError(L("Ошибка ожидания ответа SPP", "Error while waiting for SPP response"));
            char chunk[2048];
            const ssize_t received = ::recv(socket_, chunk, sizeof(chunk), 0);
            if (received == 0)
                throw makeError(L("Наушники закрыли SPP-соединение", "Headphones closed the SPP connection"));
            if (received < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                throw makeError(L("Ошибка чтения SPP: %1", "SPP read error: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            }
            buffer.append(chunk, static_cast<qsizetype>(received));
            if (buffer.size() > 65536)
                buffer = buffer.right(8192);
        }
        throw makeError(L("Наушники не подтвердили команду; состояние настройки неизвестно", "The headphones did not acknowledge the command; setting state is unknown"));
    }

    void sendCommand(int commandId, const QByteArray &payload)
    {
        if (socket_ < 0)
            throw makeError(L("Сначала подключитесь к HAYLOU S30", "Connect to HAYLOU S30 first"));
        if (payload.size() + 1 > 65535)
            throw makeError(L("Слишком длинная команда", "Command is too long"));

        const uint8_t sequence = sequence_++;
        if (sequence_ == 255)
            sequence_ = 0;
        const int length = payload.size() + 1;
        QByteArray frame(kStart, 3);
        frame.append(char(0xC0));
        frame.append(static_cast<char>(commandId));
        frame.append(static_cast<char>((length >> 8) & 0xFF));
        frame.append(static_cast<char>(length & 0xFF));
        frame.append(static_cast<char>(sequence));
        frame.append(payload);
        frame.append(kEnd, 3);
        sendAll(frame);
        readResponse(commandId, sequence);
    }

    int socket_ = -1;
    uint8_t sequence_ = 0;
    std::mutex mutex_;
    std::function<void(int)> ancUpdateCallback_;
    QByteArray eventBuffer_;
};

class WindowHeader final : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && window() && window()->windowHandle()
            && window()->windowHandle()->startSystemMove()) {
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

};

class DecoratedSlider final : public QSlider {
public:
    explicit DecoratedSlider(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSlider(orientation, parent) {}

    void setThumbColors(const QColor &fill, const QColor &outline, const QColor &grip)
    {
        fill_ = fill;
        outline_ = outline;
        grip_ = grip;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QSlider::paintEvent(event);
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        if (handle.isEmpty())
            return;

        const QPoint center(handle.center().x(), groove.isEmpty() ? handle.center().y() : groove.bottom() - 1);
        const QRectF thumb(center.x() - 8.0, center.y() - 8.0, 16.0, 16.0);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(outline_, 1.0));
        painter.setBrush(fill_);
        painter.drawRoundedRect(thumb, 3.5, 3.5);
        painter.setPen(QPen(grip_, 1.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(center.x() - 2.0, center.y() - 3.0), QPointF(center.x() - 2.0, center.y() + 3.0));
        painter.drawLine(QPointF(center.x() + 2.0, center.y() - 3.0), QPointF(center.x() + 2.0, center.y() + 3.0));
    }

private:
    QColor fill_ = QColor("#FFB300");
    QColor outline_ = QColor("#FFC247");
    QColor grip_ = QColor("#9A6810");
};

class MainWindow final : public QMainWindow {
public:
    MainWindow()
    {
        setWindowTitle(QStringLiteral("HAYLOU S30"));
        setWindowFlag(Qt::FramelessWindowHint, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        resize(660, 650);
        setMinimumSize(540, 620);
        setMaximumWidth(940);
        gEnglish.store(QSettings().value(QStringLiteral("language"), 0).toInt() == 1);
        pool_.setMaxThreadCount(1);
        pool_.setExpiryTimeout(-1);
        buildUi();
        spp_.setAncUpdateCallback([this](int mode) {
            QMetaObject::invokeMethod(this, [this, mode] { updateAncState(mode); }, Qt::QueuedConnection);
        });
        applyTheme(QSettings().value(QStringLiteral("theme"), 0).toInt());
        eventTimer_ = new QTimer(this);
        eventTimer_->setInterval(350);
        connect(eventTimer_, &QTimer::timeout, this, [this] {
            if (!connected_)
                return;
            bool expected = false;
            if (!pollPending_.compare_exchange_strong(expected, true))
                return;
            pool_.start(QRunnable::create([this] {
                bool alive = false;
                try { alive = spp_.pollEvents(); } catch (...) { }
                pollPending_.store(false);
                if (!alive) {
                    QMetaObject::invokeMethod(this, [this] {
                        if (!connected_)
                            return;
                        connected_ = false;
                        connectionStatus_->setText(L("SPP-соединение потеряно; выполняю переподключение…",
                                                     "SPP connection lost; attempting to reconnect…"));
                        status_->setText(L("Соединение потеряно", "Connection lost"));
                        setBusy(false);
                    }, Qt::QueuedConnection);
                }
            }));
        });
        eventTimer_->start();
        autoReconnectTimer_ = new QTimer(this);
        autoReconnectTimer_->setInterval(8000);
        connect(autoReconnectTimer_, &QTimer::timeout, this, [this] { attemptAutoConnect(); });
        autoReconnectTimer_->start();
        refreshLists();
    }

    ~MainWindow() override
    {
        eventTimer_->stop();
        autoReconnectTimer_->stop();
        pool_.waitForDone();
        spp_.disconnectDevice();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QMainWindow::resizeEvent(event);
        const bool compact = event->size().width() < 720;
        if (compact != compactLayout_)
            arrangeCards(compact);
    }

private:
    void buildUi()
    {
        auto *header = new WindowHeader(this);
        header->setObjectName(QStringLiteral("windowHeader"));
        auto *headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(10, 0, 6, 0);
        headerLayout->setSpacing(5);
        windowTitleLabel_ = new QLabel(QStringLiteral("HAYLOU S30"), header);
        windowTitleLabel_->setObjectName(QStringLiteral("windowCaption"));
        windowTitleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
        headerLayout->addWidget(windowTitleLabel_, 1);
        settingsButton_ = new QToolButton(header);
        auto *settingsButton = settingsButton_;
        settingsButton->setText(QStringLiteral("⚙"));
        settingsButton->setObjectName(QStringLiteral("windowControl"));
        settingsButton->setToolTip(L("Настройки оформления", "Appearance and language settings"));
        settingsButton->setPopupMode(QToolButton::InstantPopup);
        settingsButton->setFixedSize(32, 30);
        auto *themeMenu = new QMenu(settingsButton);
        themeNames_ = {L("Чёрный + натрий", "Black + Sodium"), L("Лимонное золото", "Lemon Gold"),
            L("Лёд и бирюза", "Ice Teal"), L("Медный янтарь", "Copper Amber"), QStringLiteral("Winamp Classic")};
        auto *themePanel = new QWidget(themeMenu);
        auto *themeLayout = new QVBoxLayout(themePanel);
        themeLayout->setContentsMargins(4, 4, 4, 4);
        themeLayout->setSpacing(3);
        for (int i = 0; i < themeNames_.size(); ++i) {
            auto *button = new QPushButton(themeNames_.at(i), themePanel);
            button->setMinimumWidth(210);
            themeLayout->addWidget(button);
            themeButtons_.push_back(button);
            connect(button, &QPushButton::clicked, this, [this, i, themeMenu] {
                applyTheme(i);
                themeMenu->hide();
            });
        }
        auto *themeAction = new QWidgetAction(themeMenu);
        themeAction->setDefaultWidget(themePanel);
        themeMenu->addAction(themeAction);
        languageMenu_ = themeMenu->addMenu(L("Язык", "Language"));
        auto *languageGroup = new QActionGroup(this);
        languageGroup->setExclusive(true);
        russianAction_ = languageMenu_->addAction(QStringLiteral("Русский"));
        englishAction_ = languageMenu_->addAction(QStringLiteral("English"));
        for (QAction *action : {russianAction_, englishAction_}) {
            action->setCheckable(true);
            languageGroup->addAction(action);
        }
        connect(russianAction_, &QAction::triggered, this, [this] { setLanguage(false); });
        connect(englishAction_, &QAction::triggered, this, [this] { setLanguage(true); });
        russianAction_->setChecked(!gEnglish.load());
        englishAction_->setChecked(gEnglish.load());
        themeMenu->addSeparator();
        QAction *aboutAction = themeMenu->addAction(L("О приложении · версия 1.5", "About · version 1.5"));
        aboutAction_ = aboutAction;
        connect(aboutAction, &QAction::triggered, this, [this] {
            QMessageBox::about(this, QStringLiteral("S30"),
                L("S30 · версия 1.5\nУправление Haylou S30 для Ubuntu.", "S30 · version 1.5\nHaylou S30 control for Ubuntu."));
        });
        settingsButton->setMenu(themeMenu);
        headerLayout->addWidget(settingsButton);

        minButton_ = new QPushButton(QStringLiteral("—"), header);
        auto *minButton = minButton_;
        closeButton_ = new QPushButton(QStringLiteral("×"), header);
        auto *closeButton = closeButton_;
        minButton->setObjectName(QStringLiteral("windowControl"));
        closeButton->setObjectName(QStringLiteral("windowClose"));
        for (QPushButton *button : {minButton, closeButton})
            button->setFixedSize(30, 30);
        minButton->setToolTip(L("Свернуть", "Minimize"));
        closeButton->setToolTip(L("Закрыть", "Close"));
        connect(minButton, &QPushButton::clicked, this, &QWidget::showMinimized);
        connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
        headerLayout->addWidget(minButton);
        headerLayout->addWidget(closeButton);
        header->setFixedHeight(38);
        setMenuWidget(header);

        auto *statusBar = new QStatusBar(this);
        statusBar->setSizeGripEnabled(true);
        statusBar->setFixedHeight(18);
        setStatusBar(statusBar);

        auto *scroll = new QScrollArea(this);
        scroll->setObjectName(QStringLiteral("appScroll"));
        scroll->setWidgetResizable(true);
        scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *content = new QWidget(scroll);
        content->setObjectName(QStringLiteral("appContent"));
        content->setMaximumWidth(900);
        auto *page = new QVBoxLayout(content);
        page->setContentsMargins(10, 8, 10, 10);
        page->setSpacing(8);

        connectionBox_ = new QGroupBox(L("Наушники", "Headphones"), content);
        auto *connectionBox = connectionBox_;
        auto *connectionLayout = new QHBoxLayout(connectionBox);
        connectionLayout->setContentsMargins(8, 7, 8, 7);
        deviceCombo_ = new QComboBox(connectionBox);
        deviceCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        deviceCombo_->setMinimumContentsLength(15);
        refreshButton_ = new QPushButton(L("Обновить", "Refresh"), connectionBox);
        connectButton_ = new QPushButton(L("Подключить", "Connect"), connectionBox);
        disconnectButton_ = new QPushButton(L("Отключить", "Disconnect"), connectionBox);
        disconnectButton_->setEnabled(false);
        connectionLayout->addWidget(deviceCombo_, 1);
        connectionLayout->addWidget(refreshButton_);
        connectionLayout->addWidget(connectButton_);
        connectionLayout->addWidget(disconnectButton_);
        page->addWidget(connectionBox);

        connectionStatus_ = new QLabel(L("Настройки наушников отправляются только по кнопкам применения.", "Headphone settings are sent only when you press Apply."), content);
        connectionStatus_->setObjectName(QStringLiteral("hint"));
        connectionStatus_->setWordWrap(true);
        page->addWidget(connectionStatus_);

        cards_ = new QWidget(content);
        cardsLayout_ = new QGridLayout(cards_);
        cardsLayout_->setContentsMargins(0, 0, 0, 0);
        cardsLayout_->setSpacing(8);

        ancBox_ = new QGroupBox(QStringLiteral("ANC"), cards_);
        auto *ancLayout = new QVBoxLayout(ancBox_);
        ancLayout->setContentsMargins(8, 7, 8, 7);
        ancStateLabel_ = new QLabel(L("Состояние: неизвестно", "State: unknown"), ancBox_);
        ancLayout->addWidget(ancStateLabel_);
        auto *ancControlRow = new QHBoxLayout();
        ancCombo_ = new QComboBox(ancBox_);
        ancCombo_->addItem(L("Шумоподавление", "Noise cancelling"), 1);
        ancCombo_->addItem(L("Выкл.", "Off"), 0);
        ancCombo_->addItem(L("Прозрачность", "Transparency"), 2);
        ancApply_ = new QPushButton(L("Применить", "Apply"), ancBox_);
        ancControlRow->addWidget(ancCombo_, 1);
        ancControlRow->addWidget(ancApply_);
        ancLayout->addLayout(ancControlRow);

        volumeBox_ = new QGroupBox(L("Громкость выхода Ubuntu", "Ubuntu output volume"), cards_);
        auto *volumeLayout = new QVBoxLayout(volumeBox_);
        volumeLayout->setContentsMargins(8, 7, 8, 7);
        auto *sinkRow = new QHBoxLayout();
        sinkCombo_ = new QComboBox(volumeBox_);
        sinkCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        sinkCombo_->setMinimumContentsLength(13);
        sinkRefreshButton_ = new QPushButton(QStringLiteral("↻"), volumeBox_);
        sinkRefreshButton_->setToolTip(L("Обновить Bluetooth-аудиовыходы", "Refresh Bluetooth audio outputs"));
        sinkRefreshButton_->setFixedWidth(34);
        sinkRow->addWidget(sinkCombo_, 1);
        sinkRow->addWidget(sinkRefreshButton_);
        volumeLayout->addLayout(sinkRow);
        auto *sliderRow = new QHBoxLayout();
        volumeSlider_ = new DecoratedSlider(Qt::Horizontal, volumeBox_);
        volumeSlider_->setRange(0, 100);
        volumeLabel_ = new QLabel(QStringLiteral("—%"), volumeBox_);
        volumeLabel_->setMinimumWidth(38);
        sliderRow->addWidget(volumeSlider_, 1);
        sliderRow->addWidget(volumeLabel_);
        volumeLayout->addLayout(sliderRow);
        volumeHint_ = new QLabel(L("Профиль Ubuntu / PipeWire", "Ubuntu / PipeWire profile"), volumeBox_);
        volumeHint_->setObjectName(QStringLiteral("hint"));
        volumeLayout->addWidget(volumeHint_);

        presetBox_ = new QGroupBox(L("Пресет EQ", "EQ preset"), cards_);
        auto *presetLayout = new QHBoxLayout(presetBox_);
        presetLayout->setContentsMargins(8, 7, 8, 7);
        presetCombo_ = new QComboBox(presetBox_);
        const QVector<QPair<QString, int>> presets = {
            {L("Стандартный", "Standard"), 0}, {L("Вокал", "Vocal"), 1},
            {L("Рок", "Rock"), 2}, {L("Классика", "Classical"), 3},
            {L("Поп", "Pop"), 4}, {L("Бас", "Bass"), 5},
            {L("Саббас", "Sub-bass"), 6}, {L("Мягкий", "Soft"), 7},
            {L("Улица", "Street"), 8}
        };
        for (const auto &preset : presets)
            presetCombo_->addItem(preset.first, preset.second);
        presetApply_ = new QPushButton(L("Применить", "Apply"), presetBox_);
        presetLayout->addWidget(presetCombo_, 1);
        presetLayout->addWidget(presetApply_);

        page->addWidget(cards_);
        status_ = new QLabel(L("Готово", "Ready"), content);
        status_->setObjectName(QStringLiteral("status"));
        status_->setWordWrap(true);
        page->addWidget(status_);
        page->addStretch(1);
        scroll->setWidget(content);
        setCentralWidget(scroll);
        ancGlow_ = new QGraphicsDropShadowEffect(ancStateLabel_);
        ancGlow_->setBlurRadius(18);
        ancGlow_->setOffset(0, 0);
        ancStateLabel_->setGraphicsEffect(ancGlow_);

        connect(refreshButton_, &QPushButton::clicked, this, [this] { refreshLists(); });
        connect(sinkRefreshButton_, &QPushButton::clicked, this, [this] { refreshLists(); });
        connect(connectButton_, &QPushButton::clicked, this, [this] { connectDevice(); });
        connect(disconnectButton_, &QPushButton::clicked, this, [this] { disconnectDevice(); });
        connect(ancApply_, &QPushButton::clicked, this, [this] { applyAnc(); });
        connect(presetApply_, &QPushButton::clicked, this, [this] { applyPreset(); });
        volumeApplyTimer_ = new QTimer(this);
        volumeApplyTimer_->setSingleShot(true);
        volumeApplyTimer_->setInterval(70);
        connect(volumeApplyTimer_, &QTimer::timeout, this, [this] { applyVolume(); });
        connect(volumeSlider_, &QSlider::valueChanged, this, [this](int value) {
            volumeLabel_->setText(QStringLiteral("%1%").arg(value));
            volumeApplyTimer_->start();
        });
        connect(sinkCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (index < 0)
                return;
            const int value = sinkCombo_->currentData(Qt::UserRole + 1).toInt();
            QSignalBlocker blocker(volumeSlider_);
            volumeSlider_->setValue(value);
            volumeLabel_->setText(QStringLiteral("%1%").arg(value));
        });
        arrangeCards(width() < 720);
    }

    void applyTheme(int index)
    {
        index = std::clamp(index, 0, 4);
        themeIndex_ = index;
        const ThemePalette &t = kThemes[index];
        const QString css = QStringLiteral(
            "QMainWindow,QScrollArea,QWidget#qt_scrollarea_viewport,QWidget#appContent { background-color:%1; }"
            "QMainWindow { background:%1; border:1px solid %2; border-radius:8px; }"
            "QScrollArea { border:0; background:%1; }"
            "QWidget#windowHeader { background:%3; border-bottom:1px solid %2; border-top-left-radius:7px; border-top-right-radius:7px; }"
            "QLabel#windowCaption { color:%4; font-size:13px; font-weight:800; }"
            "QPushButton#windowControl,QToolButton#windowControl { color:%5; background:transparent; border:0; border-radius:5px; padding:0; min-height:26px; font-size:15px; }"
            "QPushButton#windowControl:hover,QToolButton#windowControl:hover { color:%4; background:%2; }"
            "QPushButton#windowClose { color:%5; background:transparent; border:0; border-radius:5px; padding:0; min-height:26px; font-size:18px; }"
            "QPushButton#windowClose:hover { color:#FFFFFF; background:#C83C32; }"
            "QStatusBar { background:%1; border-top:1px solid %2; border-bottom-left-radius:7px; border-bottom-right-radius:7px; }"
            "QStatusBar::item { border:0; }"
            "QWidget { color:%5; font-size:12px; }"
            "QGroupBox { border:1px solid %2; border-radius:7px; margin-top:15px; padding-top:6px; background:%3; font-weight:600; }"
            "QGroupBox::title { subcontrol-origin:margin; subcontrol-position:top left; left:9px; top:0px; padding:0 4px; background:%3; color:%4; }"
            "QPushButton,QToolButton { color:%4; font-weight:650; background:#29271F; border:1px solid %2; border-radius:5px; padding:5px 9px; min-height:22px; }"
            "QPushButton:hover,QToolButton:hover { background:#3B3524; border-color:%4; }"
            "QPushButton:pressed,QToolButton:pressed { background:%4; color:#090806; border:2px solid %5; padding-top:7px; padding-bottom:3px; }"
            "QPushButton:disabled { color:%5; background:#302E28; border-color:#625D50; }"
            "QToolButton::menu-indicator { image:none; width:0px; }"
            "QComboBox { color:%4; font-weight:600; background:#211F19; border:1px solid %2; border-radius:4px; padding:3px; min-height:23px; }"
            "QComboBox QAbstractItemView { color:%5; background:#171611; selection-color:#090806; selection-background-color:%4; border:1px solid %2; outline:0; }"
            "QComboBox QAbstractItemView::item { color:%5; background:#171611; padding:5px 7px; min-height:20px; }"
            "QComboBox QAbstractItemView::item:selected { color:#090806; background:%4; }"
            "QMenu { color:%5; background:%3; border:1px solid %2; padding:5px; }"
            "QMenu::item { color:%5; background:transparent; padding:7px 24px 7px 10px; min-height:22px; }"
            "QMenu::item:selected { color:#090806; background:%4; }"
            "QMenu::item:checked { color:%4; font-weight:800; }"
            "QMenu::separator { height:1px; background:%2; margin:5px 6px; }"
            "QSlider::groove:horizontal { height:6px; background:transparent; border-bottom:2px dashed %2; }"
            "QSlider::handle:horizontal { width:16px; height:16px; margin:-5px 0; border-radius:3px; background:transparent; border:0; }"
            "QSlider::handle:horizontal:pressed { background:transparent; border:0; }"
            "QLabel#hint { color:%5; }"
            "QLabel#status { color:%5; padding:2px 3px; }")
            .arg(QString::fromLatin1(t.bg), QString::fromLatin1(t.edge), QString::fromLatin1(t.panel),
                 QString::fromLatin1(t.accent), QString::fromLatin1(t.bright));
        setStyleSheet(css);
        volumeSlider_->setThumbColors(QColor(QString::fromLatin1(t.accent)),
                                      QColor(QString::fromLatin1(t.bright)),
                                      QColor(QString::fromLatin1(t.glow)));
        if (auto *scroll = findChild<QScrollArea *>(QStringLiteral("appScroll"))) {
            const QColor background(QString::fromLatin1(t.bg));
            QPalette palette = scroll->palette();
            palette.setColor(QPalette::Window, background);
            palette.setColor(QPalette::Base, background);
            scroll->setPalette(palette);
            scroll->setAutoFillBackground(true);
            QWidget *viewport = scroll->viewport();
            viewport->setPalette(palette);
            viewport->setAutoFillBackground(true);
            if (QWidget *content = scroll->widget()) {
                content->setPalette(palette);
                content->setAutoFillBackground(true);
            }
        }
        QSettings().setValue(QStringLiteral("theme"), index);
        for (int i = 0; i < themeButtons_.size(); ++i) {
            const ThemePalette &theme = kThemes[i];
            themeButtons_.at(i)->setText((i == index ? QStringLiteral("✓  ") : QString()) + themeNames_.at(i));
            themeButtons_.at(i)->setStyleSheet(QStringLiteral(
                "QPushButton { color:%1; background:%2; border:1px solid %3; border-radius:6px; padding:8px 10px; text-align:left; font-weight:700; }"
                "QPushButton:hover { background:%4; border-color:%1; }"
                "QPushButton:pressed { background:%1; color:%5; }")
                .arg(QString::fromLatin1(theme.bright), QString::fromLatin1(theme.panel),
                     QString::fromLatin1(i == index ? theme.accent : theme.edge),
                     QString::fromLatin1(theme.edge), QString::fromLatin1(theme.bg)));
        }
        if (ancMode_ >= 0)
            updateAncState(ancMode_);
    }

    void setLanguage(bool english)
    {
        gEnglish.store(english);
        QSettings().setValue(QStringLiteral("language"), english ? 1 : 0);
        setWindowTitle(L("HAYLOU S30", "HAYLOU S30"));
        windowTitleLabel_->setText(L("HAYLOU S30", "HAYLOU S30"));
        settingsButton_->setToolTip(L("Настройки оформления", "Appearance and language settings"));
        minButton_->setToolTip(L("Свернуть", "Minimize"));
        closeButton_->setToolTip(L("Закрыть", "Close"));
        themeNames_ = {L("Чёрный + натрий", "Black + Sodium"), L("Лимонное золото", "Lemon Gold"),
            L("Лёд и бирюза", "Ice Teal"), L("Медный янтарь", "Copper Amber"), QStringLiteral("Winamp Classic")};
        languageMenu_->setTitle(L("Язык", "Language"));
        aboutAction_->setText(L("О приложении · версия 1.5", "About · version 1.5"));
        {
            QSignalBlocker ruBlocker(russianAction_);
            QSignalBlocker enBlocker(englishAction_);
            russianAction_->setChecked(!english);
            englishAction_->setChecked(english);
        }
        connectionBox_->setTitle(L("Наушники", "Headphones"));
        refreshButton_->setText(L("Обновить", "Refresh"));
        connectButton_->setText(L("Подключить", "Connect"));
        disconnectButton_->setText(L("Отключить", "Disconnect"));
        connectionStatus_->setText(connected_
            ? L("SPP подключён. Команды отправляются только по нажатию кнопок применения.",
                "SPP connected. Commands are sent only when you press Apply.")
            : autoConnectSuppressed_
                ? L("Автоподключение приостановлено до следующего запуска.", "Auto-connect is paused until the next launch.")
            : deviceCombo_->count() > 0
                ? L("Выберите HAYLOU S30 и нажмите «Подключить».", "Select HAYLOU S30 and press Connect.")
                : L("Сопряжённый HAYLOU S30 не найден. Сопрягите его в настройках Bluetooth Ubuntu.",
                    "Paired HAYLOU S30 not found. Pair it in Ubuntu Bluetooth settings."));
        ancStateLabel_->setText(L("Состояние: неизвестно", "State: unknown"));
        ancCombo_->setItemText(ancCombo_->findData(1), L("Шумоподавление", "Noise cancelling"));
        ancCombo_->setItemText(ancCombo_->findData(0), L("Выкл.", "Off"));
        ancCombo_->setItemText(ancCombo_->findData(2), L("Прозрачность", "Transparency"));
        ancApply_->setText(L("Применить", "Apply"));
        volumeBox_->setTitle(L("Громкость выхода Ubuntu", "Ubuntu output volume"));
        sinkRefreshButton_->setToolTip(L("Обновить Bluetooth-аудиовыходы", "Refresh Bluetooth audio outputs"));
        volumeHint_->setText(sinkCombo_->count() > 0
            ? L("Громкость Bluetooth-аудиовыхода Ubuntu / PipeWire", "Ubuntu / PipeWire Bluetooth output volume")
            : L("Профиль Ubuntu / PipeWire", "Ubuntu / PipeWire profile"));
        presetBox_->setTitle(L("Пресет EQ", "EQ preset"));
        const QString presetNames[] = {L("Стандартный", "Standard"), L("Вокал", "Vocal"), L("Рок", "Rock"),
            L("Классика", "Classical"), L("Поп", "Pop"), L("Бас", "Bass"), L("Саббас", "Sub-bass"),
            L("Мягкий", "Soft"), L("Улица", "Street")};
        for (int i = 0; i < presetCombo_->count(); ++i)
            presetCombo_->setItemText(i, presetNames[i]);
        ancApply_->setText(L("Применить", "Apply"));
        presetApply_->setText(L("Применить", "Apply"));
        status_->setText(L("Готово", "Ready"));
        if (ancMode_ >= 0)
            updateAncState(ancMode_);
        applyTheme(themeIndex_);
    }

    void updateAncState(int mode)
    {
        ancMode_ = mode;
        const QString names[] = {L("Выкл.", "Off"), L("Шумоподавление", "Noise cancelling"),
                                 L("Прозрачность", "Transparency")};
        static const QColor colors[] = {QColor("#FF5252"), QColor("#55E36B"), QColor("#FFD54F")};
        if (mode < 0 || mode > 2)
            return;
        ancStateLabel_->setText(L("ANC · %1", "ANC · %1").arg(names[mode]));
        ancStateLabel_->setStyleSheet(QStringLiteral("QLabel { color:%1; font-weight:800; }").arg(colors[mode].name()));
        ancGlow_->setColor(colors[mode]);
        QSignalBlocker blocker(ancCombo_);
        const int index = ancCombo_->findData(mode);
        if (index >= 0)
            ancCombo_->setCurrentIndex(index);
    }

    void arrangeCards(bool compact)
    {
        compactLayout_ = compact;
        cardsLayout_->removeWidget(ancBox_);
        cardsLayout_->removeWidget(volumeBox_);
        cardsLayout_->removeWidget(presetBox_);
        if (compact) {
            cardsLayout_->addWidget(ancBox_, 0, 0);
            cardsLayout_->addWidget(volumeBox_, 1, 0);
            cardsLayout_->addWidget(presetBox_, 2, 0);
        } else {
            cardsLayout_->addWidget(ancBox_, 0, 0);
            cardsLayout_->addWidget(volumeBox_, 0, 1);
            cardsLayout_->addWidget(presetBox_, 1, 0, 1, 2);
        }
        cardsLayout_->setColumnStretch(0, 1);
        cardsLayout_->setColumnStretch(1, compact ? 0 : 1);
    }

    void setBusy(bool busy)
    {
        refreshButton_->setEnabled(!busy);
        sinkRefreshButton_->setEnabled(!busy);
        connectButton_->setEnabled(!busy && !connected_ && deviceCombo_->count() > 0);
        disconnectButton_->setEnabled(!busy && connected_);
        deviceCombo_->setEnabled(!busy && !connected_);
        ancApply_->setEnabled(!busy && connected_);
        presetApply_->setEnabled(!busy && connected_);
        volumeSlider_->setEnabled(sinkCombo_->count() > 0);
    }

    void startTask(const QString &label, std::function<QString()> work,
                   std::function<void(bool, const QString &)> completion = {})
    {
        if (busy_)
            return;
        busy_ = true;
        setBusy(true);
        status_->setText(label + L("…", "…"));
        QPointer<MainWindow> guard(this);
        pool_.start(QRunnable::create([guard, label, work = std::move(work), completion = std::move(completion)]() mutable {
            bool success = true;
            QString result;
            try {
                result = work();
            } catch (const std::exception &error) {
                success = false;
                result = QString::fromUtf8(error.what());
            } catch (...) {
                success = false;
                result = L("Неизвестная ошибка", "Unknown error");
            }
            if (!guard)
                return;
            QMetaObject::invokeMethod(guard, [guard, label, success, result, completion = std::move(completion)]() mutable {
                if (!guard)
                    return;
                guard->busy_ = false;
                guard->setBusy(false);
                guard->status_->setText(success ? result : L("Ошибка: %1", "Error: %1").arg(result));
                if (completion)
                    completion(success, result);
            }, Qt::QueuedConnection);
        }));
    }

    void refreshLists()
    {
        auto lists = std::make_shared<SystemLists>();
        startTask(L("Обновляю список устройств", "Refreshing devices"), [lists] {
            try { lists->devices = findS30Devices(); }
            catch (const std::exception &error) { lists->deviceError = QString::fromUtf8(error.what()); }
            try { lists->sinks = findS30AudioSinks(); }
            catch (const std::exception &error) { lists->sinkError = QString::fromUtf8(error.what()); }
            return L("Списки обновлены", "Lists updated");
        }, [this, lists](bool, const QString &) {
            {
                QSignalBlocker blocker(deviceCombo_);
                deviceCombo_->clear();
                for (const BluetoothDevice &device : lists->devices)
                    deviceCombo_->addItem(device.name, device.address);
            }
            {
                QSignalBlocker blocker(sinkCombo_);
                sinkCombo_->clear();
                for (const AudioSink &sink : lists->sinks) {
                    sinkCombo_->addItem(sink.description, sink.name);
                    sinkCombo_->setItemData(sinkCombo_->count() - 1, sink.volume, Qt::UserRole + 1);
                }
            }
            connectButton_->setEnabled(!connected_ && !lists->devices.isEmpty());
            volumeSlider_->setEnabled(!busy_ && !lists->sinks.isEmpty());
            if (lists->devices.isEmpty()) {
                connectionStatus_->setText(lists->deviceError.isEmpty()
                    ? L("Сопряжённый HAYLOU S30 не найден. Сопрягите его в настройках Bluetooth Ubuntu.",
                        "Paired HAYLOU S30 not found. Pair it in Ubuntu Bluetooth settings.")
                    : lists->deviceError);
            } else if (!connected_) {
                connectionStatus_->setText(L("Выберите HAYLOU S30 и нажмите «Подключить».", "Select HAYLOU S30 and press Connect."));
            }
            if (lists->sinks.isEmpty()) {
                volumeHint_->setText(lists->sinkError.isEmpty()
                    ? L("Bluetooth-аудиовыход S30 не найден. Подключите наушники как аудиоустройство.",
                        "S30 Bluetooth audio output not found. Connect the headphones as an audio device.")
                    : lists->sinkError);
            } else {
                volumeHint_->setText(L("Громкость Bluetooth-аудиовыхода Ubuntu / PipeWire", "Ubuntu / PipeWire Bluetooth output volume"));
                const int value = sinkCombo_->currentData(Qt::UserRole + 1).toInt();
                QSignalBlocker blocker(volumeSlider_);
                volumeSlider_->setValue(value);
                volumeLabel_->setText(QStringLiteral("%1%").arg(value));
            }
            if (!connected_ && !autoConnectSuppressed_ && !lists->devices.isEmpty())
                QTimer::singleShot(250, this, [this] { attemptAutoConnect(); });
        });
    }

    void connectDevice()
    {
        if (deviceCombo_->currentIndex() < 0)
            return;
        autoConnectSuppressed_ = false;
        const QString address = deviceCombo_->currentData().toString();
        beginConnect(address, false);
    }

    void attemptAutoConnect()
    {
        if (autoConnectSuppressed_ || connected_ || busy_)
            return;
        if (deviceCombo_->count() == 0) {
            refreshLists();
            return;
        }
        beginConnect(deviceCombo_->currentData().toString(), true);
    }

    void beginConnect(const QString &address, bool automatic)
    {
        if (address.isEmpty() || connected_ || busy_)
            return;
        startTask(automatic ? L("Автоподключаю HAYLOU S30", "Auto-connecting to HAYLOU S30")
                            : L("Подключаю SPP", "Connecting SPP"),
                  [this, address] { return spp_.connectDevice(address); },
                  [this, automatic](bool success, const QString &message) {
            connected_ = success;
            setBusy(false);
            connectionStatus_->setText(success
                ? L("SPP подключён. Команды отправляются только по нажатию кнопок применения.",
                    "SPP connected. Commands are sent only when you press Apply.")
                : automatic
                    ? L("HAYLOU S30 пока недоступны; повторю подключение автоматически.",
                        "HAYLOU S30 is not available yet; I will retry automatically.")
                    : message);
        });
    }

    void disconnectDevice()
    {
        autoConnectSuppressed_ = true;
        startTask(L("Отключаю SPP", "Disconnecting SPP"), [this] { return spp_.disconnectDevice(); },
                  [this](bool success, const QString &) {
            if (success) {
                connected_ = false;
                connectionStatus_->setText(L("SPP отключён; автоподключение приостановлено до следующего запуска.",
                                             "SPP disconnected; auto-connect is paused until the next launch."));
            }
            setBusy(false);
        });
    }

    void applyAnc()
    {
        if (!connected_) {
            QMessageBox::information(this, L("Нет соединения", "Not connected"), L("Сначала подключитесь к наушникам.", "Connect to the headphones first."));
            return;
        }
        const int mode = ancCombo_->currentData().toInt();
        const QString name = ancCombo_->currentText();
        startTask(L("Применяю ANC", "Applying ANC"), [this, mode, name] {
            return name + QStringLiteral(": ") + spp_.setAnc(mode);
        }, [this, mode](bool success, const QString &) {
            if (success)
                updateAncState(mode);
        });
    }

    void applyPreset()
    {
        if (!connected_) {
            QMessageBox::information(this, L("Нет соединения", "Not connected"), L("Сначала подключитесь к наушникам.", "Connect to the headphones first."));
            return;
        }
        const int preset = presetCombo_->currentData().toInt();
        const QString name = presetCombo_->currentText();
        startTask(L("Применяю EQ", "Applying EQ"), [this, preset, name] { return name + QStringLiteral(": ") + spp_.setPreset(preset); });
    }

    void applyVolume()
    {
        if (sinkCombo_->currentIndex() < 0)
            return;
        if (busy_) {
            volumeApplyTimer_->start();
            return;
        }
        const QString sink = sinkCombo_->currentData().toString();
        const int volume = volumeSlider_->value();
        startTask(L("Меняю громкость", "Changing volume"), [sink, volume] {
            runProcess(QStringLiteral("pactl"), {QStringLiteral("set-sink-volume"), sink, QStringLiteral("%1%").arg(volume)});
            return L("Громкость выхода HAYLOU S30: %1%", "HAYLOU S30 output volume: %1%").arg(volume);
        });
    }

    QThreadPool pool_;
    SppClient spp_;
    bool busy_ = false;
    bool connected_ = false;
    bool compactLayout_ = false;
    bool autoConnectSuppressed_ = false;
    int themeIndex_ = 0;
    QGridLayout *cardsLayout_ = nullptr;
    QWidget *cards_ = nullptr;
    QGroupBox *ancBox_ = nullptr;
    QGroupBox *volumeBox_ = nullptr;
    QGroupBox *presetBox_ = nullptr;
    QGroupBox *connectionBox_ = nullptr;
    QComboBox *deviceCombo_ = nullptr;
    QComboBox *ancCombo_ = nullptr;
    QComboBox *sinkCombo_ = nullptr;
    QComboBox *presetCombo_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QPushButton *connectButton_ = nullptr;
    QPushButton *disconnectButton_ = nullptr;
    QPushButton *ancApply_ = nullptr;
    QPushButton *sinkRefreshButton_ = nullptr;
    QPushButton *presetApply_ = nullptr;
    QPushButton *minButton_ = nullptr;
    QPushButton *closeButton_ = nullptr;
    QLabel *windowTitleLabel_ = nullptr;
    QToolButton *settingsButton_ = nullptr;
    DecoratedSlider *volumeSlider_ = nullptr;
    QLabel *volumeLabel_ = nullptr;
    QLabel *volumeHint_ = nullptr;
    QLabel *connectionStatus_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *ancStateLabel_ = nullptr;
    QGraphicsDropShadowEffect *ancGlow_ = nullptr;
    QStringList themeNames_;
    QVector<QPushButton *> themeButtons_;
    QMenu *languageMenu_ = nullptr;
    QAction *russianAction_ = nullptr;
    QAction *englishAction_ = nullptr;
    QAction *aboutAction_ = nullptr;
    int ancMode_ = -1;
    QTimer *eventTimer_ = nullptr;
    QTimer *volumeApplyTimer_ = nullptr;
    QTimer *autoReconnectTimer_ = nullptr;
    std::atomic_bool pollPending_{false};
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("S30"));
    app.setOrganizationName(QStringLiteral("Local"));
    app.setDesktopFileName(QStringLiteral("haylou-s30-control"));
    gEnglish.store(QSettings().value(QStringLiteral("language"), 0).toInt() == 1);
    const QString iconPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/icons/hicolor/scalable/apps/s30.svg");
    app.setWindowIcon(QIcon(iconPath));

    QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtimeDir.isEmpty())
        runtimeDir = QDir::tempPath();
    const QString lockPath = runtimeDir + QStringLiteral("/haylou-s30-control-%1.lock").arg(getuid());
    QLockFile instanceLock(lockPath);
    instanceLock.setStaleLockTime(30000);
    if (!instanceLock.tryLock(0)) {
        if (instanceLock.error() == QLockFile::LockFailedError)
            return 0;
        QMessageBox::critical(nullptr, L("Не удалось запустить S30", "Could not start S30"),
                              L("Не удалось создать блокировку единственного экземпляра приложения.",
                                "Could not acquire the single-instance application lock."));
        return 1;
    }

    MainWindow window;
    window.show();
    return app.exec();
}
