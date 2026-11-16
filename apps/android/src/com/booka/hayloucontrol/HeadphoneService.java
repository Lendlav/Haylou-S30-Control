package com.booka.hayloucontrol;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.Context;
import android.content.Intent;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.graphics.drawable.Icon;
import android.util.Log;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Handler;
import android.os.Looper;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Method;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class HeadphoneService extends Service {
    public static final String ACTION_STATUS = "com.booka.hayloucontrol.STATUS";
    public static final String ACTION_CONNECT = "com.booka.hayloucontrol.CONNECT";
    public static final String ACTION_DISCONNECT = "com.booka.hayloucontrol.DISCONNECT";
    public static final String ACTION_SET_ANC = "com.booka.hayloucontrol.SET_ANC";
    public static final String ACTION_SET_EQ = "com.booka.hayloucontrol.SET_EQ";
    public static final String ACTION_EQ_NEXT = "com.booka.hayloucontrol.EQ_NEXT";
    public static final String ACTION_EQ_PREVIOUS = "com.booka.hayloucontrol.EQ_PREVIOUS";
    public static final String ACTION_STATE = "com.booka.hayloucontrol.STATE";
    private static final String MEDIA_ANC_OFF = "com.booka.hayloucontrol.MEDIA_ANC_OFF";
    private static final String MEDIA_ANC_NOISE = "com.booka.hayloucontrol.MEDIA_ANC_NOISE";
    private static final String MEDIA_ANC_TRANSPARENCY = "com.booka.hayloucontrol.MEDIA_ANC_TRANSPARENCY";

    public static final String EXTRA_ADDRESS = "address";
    public static final String EXTRA_NAME = "device_name";
    public static final String EXTRA_VALUE = "value";
    public static final String EXTRA_STATUS = "status";

    private static final String CHANNEL_ID = "haylou_s30_controls";
    private static final int NOTIFICATION_ID = 7301;
    private static final String TAG = "HaylouS30Control";
    // HAYLOU S30 uses Haylou's service UUID; 0x1101 opens a different RFCOMM endpoint.
    private static final UUID SPP_UUID = UUID.fromString("0000FD3D-0000-1000-8000-00805F9B34FB");
    private static final byte[] START = {(byte) 0xaa, (byte) 0xbb, (byte) 0xcc};
    private static final byte[] END = {(byte) 0xdd, (byte) 0xee, (byte) 0xff};
    private static final String[] ANC_NAMES = {"Выкл.", "Шумоподавление", "Прозрачность"};
    private static final String[] EQ_NAMES = {
            "Стандартный", "Вокал", "Рок", "Классика", "Поп", "Бас", "Саббас", "Мягкий", "Улица"
    };

    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private BluetoothSocket socket;
    private InputStream input;
    private OutputStream output;
    private final Object responseMonitor = new Object();
    private byte[] receivedBuffer = new byte[0];
    private volatile boolean readerRunning;
    private volatile String readerError;
    private volatile long receivedBytes;
    private Thread readerThread;
    private MediaSession mediaSession;
    private String deviceName = "HAYLOU S30";
    private String status = "Отключено";
    private boolean connected;
    private boolean connecting;
    private int sequence;
    private int ancMode = -1;
    private int eqPreset = -1;

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannelCompat();
        createMediaSession();
    }

    @Override
    public int onStartCommand(final Intent intent, int flags, final int startId) {
        if (intent == null || intent.getAction() == null) {
            stopSelf(startId);
            return START_NOT_STICKY;
        }
        String action = intent.getAction();
        if (ACTION_CONNECT.equals(action)) {
            final String address = intent.getStringExtra(EXTRA_ADDRESS);
            final String requestedName = intent.getStringExtra(EXTRA_NAME);
            if (connecting || connected) {
                publishStatus();
                return START_NOT_STICKY;
            }
            if (requestedName != null) deviceName = requestedName;
            connecting = true;
            status = "Подключаюсь к " + deviceName + "…";
            startForeground(NOTIFICATION_ID, buildNotification());
            publishStatus();
            worker.execute(new Runnable() {
                @Override public void run() { connect(address, startId); }
            });
            return START_NOT_STICKY;
        }
        if (ACTION_DISCONNECT.equals(action)) {
            status = "Отключаюсь…";
            publishStatus();
            worker.execute(new Runnable() {
                @Override public void run() {
                    closeSocket();
                    connected = false;
                    connecting = false;
                    status = "Отключено";
                    mainHandler.post(new Runnable() {
                        @Override public void run() {
                            stopForeground(true);
                            if (mediaSession != null) mediaSession.setActive(false);
                            publishStatus();
                            stopSelf(startId);
                        }
                    });
                }
            });
            return START_NOT_STICKY;
        }
        if (ACTION_STATE.equals(action)) {
            publishStatus();
            if (!connected && !connecting) stopSelf(startId);
            return START_NOT_STICKY;
        }
        if (ACTION_SET_ANC.equals(action)) {
            final int mode = intent.getIntExtra(EXTRA_VALUE, 0);
            if (!connected && !connecting) {
                status = "Сначала подключитесь к HAYLOU S30.";
                publishStatus();
                stopSelf(startId);
                return START_NOT_STICKY;
            }
            worker.execute(new Runnable() {
                @Override public void run() { applyAnc(mode); }
            });
            return START_NOT_STICKY;
        }
        if (ACTION_SET_EQ.equals(action)) {
            final int preset = intent.getIntExtra(EXTRA_VALUE, 0);
            if (!connected && !connecting) {
                status = "Сначала подключитесь к HAYLOU S30.";
                publishStatus();
                stopSelf(startId);
                return START_NOT_STICKY;
            }
            worker.execute(new Runnable() {
                @Override public void run() { applyEq(preset); }
            });
            return START_NOT_STICKY;
        }
        if (ACTION_EQ_NEXT.equals(action)) {
            if (!connected && !connecting) {
                status = "Сначала подключитесь к HAYLOU S30.";
                publishStatus();
                stopSelf(startId);
                return START_NOT_STICKY;
            }
            worker.execute(new Runnable() {
                @Override public void run() { applyEq((eqPreset + 1) % EQ_NAMES.length); }
            });
            return START_NOT_STICKY;
        }
        if (ACTION_EQ_PREVIOUS.equals(action)) {
            if (!connected && !connecting) {
                status = "Сначала подключитесь к HAYLOU S30.";
                publishStatus();
                stopSelf(startId);
                return START_NOT_STICKY;
            }
            worker.execute(new Runnable() {
                @Override public void run() { applyEq((eqPreset + EQ_NAMES.length - 1) % EQ_NAMES.length); }
            });
            return START_NOT_STICKY;
        }
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    @Override
    public void onDestroy() {
        worker.shutdownNow();
        closeSocket();
        if (mediaSession != null) {
            mediaSession.setActive(false);
            mediaSession.release();
            mediaSession = null;
        }
        super.onDestroy();
    }

    private void connect(String address, final int startId) {
        BluetoothSocket candidate = null;
        try {
            if (address == null || address.length() == 0) throw new Exception("Не указан Bluetooth-адрес наушников.");
            BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
            if (adapter == null || !adapter.isEnabled()) throw new Exception("Включите Bluetooth в настройках телефона.");
            BluetoothDevice device = adapter.getRemoteDevice(address);
            // Match the official Haylou client: its SPP socket is insecure RFCOMM.
            candidate = device.createInsecureRfcommSocketToServiceRecord(SPP_UUID);
            candidate.connect();
            Log.i(TAG, "SPP RFCOMM connection established");
            socket = candidate;
            input = candidate.getInputStream();
            output = candidate.getOutputStream();
            synchronized (responseMonitor) {
                receivedBuffer = new byte[0];
                readerError = null;
                receivedBytes = 0;
            }
            startReader(input);
            sequence = 0;
            connected = true;
            connecting = false;
            android.content.SharedPreferences saved = getSharedPreferences("device_state", MODE_PRIVATE);
            ancMode = saved.getInt("anc_mode", -1);
            eqPreset = saved.getInt("eq_preset", -1);
            status = connectedStateText();
            mainHandler.post(new Runnable() {
                @Override public void run() {
                    activateMediaSession();
                    publishStatus();
                }
            });
        } catch (final Exception error) {
            Log.e(TAG, "SPP connection failed", error);
            readerRunning = false;
            synchronized (responseMonitor) { responseMonitor.notifyAll(); }
            if (candidate != null) {
                try { candidate.close(); } catch (Exception ignored) { }
            }
            socket = null;
            input = null;
            output = null;
            readerThread = null;
            connected = false;
            connecting = false;
            status = "Подключение не удалось: " + safeMessage(error);
            mainHandler.post(new Runnable() {
                @Override public void run() {
                    stopForeground(true);
                    publishStatus();
                    stopSelf(startId);
                }
            });
        }
    }

    private void applyAnc(int mode) {
        if (mode < 0 || mode > 2) return;
        try {
            byte[] payload = {2, 4, (byte) mode};
            sendCommand(0x08, payload);
            ancMode = mode;
            getSharedPreferences("device_state", MODE_PRIVATE).edit().putInt("anc_mode", mode).apply();
            status = connectedStateText();
            updateAfterCommand();
        } catch (Exception error) {
            Log.e(TAG, "ANC command failed", error);
            status = safeMessage(error);
            updateAfterCommand();
        }
    }

    private void applyEq(int preset) {
        if (preset < 0 || preset >= EQ_NAMES.length) return;
        try {
            // The regular Haylou sound screen sends EQ as device config 7 via command 0xF2.
            // The 0x08 / [2, 2, preset] packet belongs to an internal test path and can
            // receive an ACK without changing the active production EQ preset.
            byte[] payload = {3, 0, 7, (byte) preset};
            sendCommand(0xF2, payload);
            eqPreset = preset;
            getSharedPreferences("device_state", MODE_PRIVATE).edit().putInt("eq_preset", preset).apply();
            status = connectedStateText();
            updateAfterCommand();
        } catch (Exception error) {
            Log.e(TAG, "EQ command failed", error);
            status = safeMessage(error);
            updateAfterCommand();
        }
    }

    private void sendCommand(int command, byte[] payload) throws Exception {
        if (!connected || socket == null || output == null || input == null)
            throw new Exception("Сначала подключитесь к HAYLOU S30.");
        int currentSequence = sequence;
        sequence = (sequence + 1) % 255;
        int length = payload.length + 1;
        ByteArrayOutputStream packet = new ByteArrayOutputStream();
        packet.write(START);
        packet.write(0xc0);
        packet.write(command);
        packet.write((length >>> 8) & 0xff);
        packet.write(length & 0xff);
        packet.write(currentSequence);
        packet.write(payload);
        packet.write(END);
        Log.i(TAG, "TX cmd=0x" + hexByte(command) + " seq=" + currentSequence + " packet=" + toHex(packet.toByteArray()));
        output.write(packet.toByteArray());
        output.flush();
        readResponse(command, currentSequence);
    }

    private void readResponse(int command, int expectedSequence) throws Exception {
        long deadline = System.currentTimeMillis() + 4000;
        synchronized (responseMonitor) {
            while (System.currentTimeMillis() < deadline) {
                int start = find(receivedBuffer, START, 0);
                if (start >= 0) {
                    if (start > 0) receivedBuffer = slice(receivedBuffer, start, receivedBuffer.length);
                    int end = find(receivedBuffer, END, 8);
                    if (end >= 0) {
                        int frameLength = end + END.length;
                        byte[] frame = slice(receivedBuffer, 0, frameLength);
                        receivedBuffer = slice(receivedBuffer, frameLength, receivedBuffer.length);
                        if (frameLength >= 12 && (frame[4] & 0xff) == command
                                && (frame[8] & 0xff) == expectedSequence) {
                            int result = frame[7] & 0xff;
                            Log.i(TAG, "RX frame=" + toHex(frame));
                            if (result != 0) throw new Exception("Наушники отклонили команду (код " + result + ").");
                            return;
                        }
                        Log.w(TAG, "Unmatched SPP frame=" + toHex(frame)
                                + " expected cmd=0x" + hexByte(command) + " seq=" + expectedSequence);
                        continue;
                    }
                } else if (receivedBuffer.length > 2) {
                    receivedBuffer = slice(receivedBuffer, receivedBuffer.length - 2, receivedBuffer.length);
                }

                if (readerError != null) throw new Exception(readerError);
                long remaining = deadline - System.currentTimeMillis();
                if (remaining > 0) responseMonitor.wait(remaining);
            }
        }
        Log.w(TAG, "SPP response timeout cmd=0x" + hexByte(command) + " seq=" + expectedSequence
                + " received=" + receivedBytes + " buffered=" + toHex(receivedBuffer));
        throw new Exception("Наушники не подтвердили команду; состояние настройки неизвестно.");
    }

    private void startReader(final InputStream stream) {
        readerRunning = true;
        readerThread = new Thread(new Runnable() {
            @Override public void run() {
                byte[] chunk = new byte[2048];
                try {
                    while (readerRunning) {
                        int count = stream.read(chunk);
                        if (count < 0) throw new Exception("Наушники закрыли SPP-соединение.");
                        if (count == 0) continue;
                        byte[] bytes = slice(chunk, 0, count);
                        Log.i(TAG, "RX chunk=" + toHex(bytes));
                        synchronized (responseMonitor) {
                            receivedBytes += count;
                            byte[] enlarged = new byte[receivedBuffer.length + count];
                            System.arraycopy(receivedBuffer, 0, enlarged, 0, receivedBuffer.length);
                            System.arraycopy(chunk, 0, enlarged, receivedBuffer.length, count);
                            receivedBuffer = enlarged.length > 65536
                                    ? slice(enlarged, enlarged.length - 8192, enlarged.length)
                                    : enlarged;
                            consumeStatusEvents();
                            responseMonitor.notifyAll();
                        }
                    }
                } catch (Exception error) {
                    if (readerRunning) {
                        readerError = safeMessage(error);
                        Log.w(TAG, "SPP reader stopped", error);
                        synchronized (responseMonitor) { responseMonitor.notifyAll(); }
                    }
                }
            }
        }, "HaylouS30-SPP-reader");
        readerThread.setDaemon(true);
        readerThread.start();
    }

    /** Device initiated 0x0E runtime event; TLV 0x02 0x08 carries ANC mode 0..2. */
    private void consumeStatusEvents() {
        int cursor = 0;
        while (true) {
            int start = find(receivedBuffer, START, cursor);
            if (start < 0) {
                if (receivedBuffer.length > 2) receivedBuffer = slice(receivedBuffer, receivedBuffer.length - 2, receivedBuffer.length);
                return;
            }
            if (receivedBuffer.length - start < 8) {
                if (start > 0) receivedBuffer = slice(receivedBuffer, start, receivedBuffer.length);
                return;
            }
            int bodyLength = ((receivedBuffer[start + 5] & 0xff) << 8) | (receivedBuffer[start + 6] & 0xff);
            int frameLength = 7 + bodyLength + END.length;
            if (bodyLength < 1 || frameLength > 128) { cursor = start + 3; continue; }
            if (receivedBuffer.length - start < frameLength) {
                if (start > 0) receivedBuffer = slice(receivedBuffer, start, receivedBuffer.length);
                return;
            }
            if (find(receivedBuffer, END, start + frameLength - END.length) != start + frameLength - END.length) {
                cursor = start + 3; continue;
            }
            int opcode = receivedBuffer[start + 4] & 0xff;
            if (opcode == 0x0e) {
                for (int i = start + 8; i + 2 < start + frameLength - END.length; i++) {
                    if ((receivedBuffer[i] & 0xff) == 0x02 && (receivedBuffer[i + 1] & 0xff) == 0x08) {
                        int mode = receivedBuffer[i + 2] & 0xff;
                        if (mode >= 0 && mode <= 2) {
                            ancMode = mode;
                            getSharedPreferences("device_state", MODE_PRIVATE).edit().putInt("anc_mode", mode).apply();
                            status = connectedStateText();
                            Log.i(TAG, "Device ANC state event mode=" + mode);
                            mainHandler.post(new Runnable() {
                                @Override public void run() { publishStatus(); }
                            });
                        }
                        break;
                    }
                }
                byte[] before = slice(receivedBuffer, 0, start);
                byte[] after = slice(receivedBuffer, start + frameLength, receivedBuffer.length);
                byte[] remainder = new byte[before.length + after.length];
                System.arraycopy(before, 0, remainder, 0, before.length);
                System.arraycopy(after, 0, remainder, before.length, after.length);
                receivedBuffer = remainder;
                cursor = 0;
            } else cursor = start + frameLength;
            if (cursor >= receivedBuffer.length) return;
        }
    }

    private int find(byte[] data, byte[] pattern, int from) {
        for (int i = Math.max(0, from); i <= data.length - pattern.length; i++) {
            boolean match = true;
            for (int j = 0; j < pattern.length; j++) {
                if (data[i + j] != pattern[j]) { match = false; break; }
            }
            if (match) return i;
        }
        return -1;
    }

    private byte[] slice(byte[] data, int start, int end) {
        int length = Math.max(0, end - start);
        byte[] result = new byte[length];
        if (length > 0) System.arraycopy(data, start, result, 0, length);
        return result;
    }

    private String toHex(byte[] bytes) {
        StringBuilder result = new StringBuilder(bytes.length * 3);
        for (byte value : bytes) result.append(hexByte(value & 0xff)).append(' ');
        return result.toString().trim();
    }

    private String hexByte(int value) {
        String hex = Integer.toHexString(value & 0xff).toUpperCase(java.util.Locale.ROOT);
        return hex.length() == 1 ? "0" + hex : hex;
    }

    private void updateAfterCommand() {
        mainHandler.post(new Runnable() {
            @Override public void run() {
                publishStatus();
            }
        });
    }

    private void createMediaSession() {
        mediaSession = new MediaSession(this, "HaylouS30Control");
        mediaSession.setCallback(new MediaSession.Callback() {
            @Override public void onCustomAction(String action, Bundle extras) {
                Intent command = new Intent(HeadphoneService.this, HeadphoneService.class);
                if (MEDIA_ANC_OFF.equals(action) || MEDIA_ANC_NOISE.equals(action) || MEDIA_ANC_TRANSPARENCY.equals(action)) {
                    command.setAction(ACTION_SET_ANC);
                    int mode = MEDIA_ANC_OFF.equals(action) ? 0 : MEDIA_ANC_NOISE.equals(action) ? 1 : 2;
                    command.putExtra(EXTRA_VALUE, mode);
                } else {
                    command.setAction(action);
                }
                HeadphoneService.this.onStartCommand(command, 0, 0);
            }
        }, mainHandler);
        mediaSession.setFlags(MediaSession.FLAG_HANDLES_TRANSPORT_CONTROLS | MediaSession.FLAG_HANDLES_MEDIA_BUTTONS);
    }

    private void activateMediaSession() {
        if (mediaSession == null) return;
        mediaSession.setMetadata(new MediaMetadata.Builder()
                .putString(MediaMetadata.METADATA_KEY_TITLE, "HAYLOU S30")
                .putString(MediaMetadata.METADATA_KEY_ARTIST, "Управление наушниками")
                .build());
        updatePlaybackState();
        mediaSession.setActive(true);
    }

    private void updatePlaybackState() {
        if (mediaSession == null) return;
        PlaybackState.Builder state = new PlaybackState.Builder()
                .setState(PlaybackState.STATE_PAUSED, 0, 0)
                .setActions(0);
        state.addCustomAction(new PlaybackState.CustomAction.Builder(MEDIA_ANC_OFF, "ANC выкл.", R.drawable.ic_anc_off)
                .setExtras(bundleFor(0)).build());
        state.addCustomAction(new PlaybackState.CustomAction.Builder(MEDIA_ANC_NOISE, "Шумоподавление", R.drawable.ic_anc_on)
                .setExtras(bundleFor(1)).build());
        state.addCustomAction(new PlaybackState.CustomAction.Builder(MEDIA_ANC_TRANSPARENCY, "Прозрачность", R.drawable.ic_transparency)
                .setExtras(bundleFor(2)).build());
        state.addCustomAction(new PlaybackState.CustomAction.Builder(ACTION_EQ_PREVIOUS, "EQ назад", R.drawable.ic_eq_prev).build());
        state.addCustomAction(new PlaybackState.CustomAction.Builder(ACTION_EQ_NEXT, "EQ далее", R.drawable.ic_eq_next).build());
        mediaSession.setPlaybackState(state.build());
    }

    private Bundle bundleFor(int value) {
        Bundle extras = new Bundle();
        extras.putInt(EXTRA_VALUE, value);
        return extras;
    }

    private void createNotificationChannelCompat() {
        if (Build.VERSION.SDK_INT < 26) return;
        try {
            Class<?> channelClass = Class.forName("android.app.NotificationChannel");
            Object constructor = channelClass.getConstructor(String.class, CharSequence.class, int.class);
            Method create = constructor.getClass().getMethod("newInstance", Object[].class);
            Object channel = create.invoke(constructor, (Object) new Object[]{CHANNEL_ID, "HAYLOU S30", Integer.valueOf(2)});
            channelClass.getMethod("setLockscreenVisibility", int.class).invoke(channel, Notification.VISIBILITY_PUBLIC);
            channelClass.getMethod("setShowBadge", boolean.class).invoke(channel, false);
            Object manager = getSystemService(Context.NOTIFICATION_SERVICE);
            manager.getClass().getMethod("createNotificationChannel", channelClass).invoke(manager, channel);
        } catch (Exception ignored) { }
    }

    private Notification.Builder notificationBuilder() {
        Notification.Builder builder = new Notification.Builder(this);
        if (Build.VERSION.SDK_INT >= 26) {
            try { builder.getClass().getMethod("setChannelId", String.class).invoke(builder, CHANNEL_ID); }
            catch (Exception ignored) { }
        }
        return builder;
    }

    private Notification buildNotification() {
        Intent open = new Intent(this, MainActivity.class);
        open.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        PendingIntent content = PendingIntent.getActivity(this, 10, open, pendingFlags());
        Notification.Builder builder = notificationBuilder()
                .setSmallIcon(R.drawable.ic_headphones)
                .setContentTitle(deviceName)
                .setContentText(notificationText())
                .setContentIntent(content)
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setCategory(Notification.CATEGORY_TRANSPORT)
                .setOngoing(true)
                .setShowWhen(false)
                .setColor(getUiAccent());

        addNotificationAction(builder, R.drawable.ic_anc_off, "ANC выкл.", servicePending(ACTION_SET_ANC, 0, 20));
        addNotificationAction(builder, R.drawable.ic_anc_on, "Шумоподавление", servicePending(ACTION_SET_ANC, 1, 21));
        addNotificationAction(builder, R.drawable.ic_transparency, "Прозрачность", servicePending(ACTION_SET_ANC, 2, 22));
        addNotificationAction(builder, R.drawable.ic_eq_prev, "EQ назад", servicePending(ACTION_EQ_PREVIOUS, 0, 23));
        addNotificationAction(builder, R.drawable.ic_eq_next, "EQ далее", servicePending(ACTION_EQ_NEXT, 0, 24));

        Notification.MediaStyle style = new Notification.MediaStyle();
        if (mediaSession != null) style.setMediaSession(mediaSession.getSessionToken());
        style.setShowActionsInCompactView(0, 1, 2);
        builder.setStyle(style);
        return builder.build();
    }

    private void addNotificationAction(Notification.Builder builder, int icon, String title, PendingIntent pendingIntent) {
        Icon actionIcon = Icon.createWithResource(this, icon);
        if (Build.VERSION.SDK_INT >= 26) actionIcon = actionIcon.setTint(getUiAccent());
        builder.addAction(new Notification.Action.Builder(actionIcon, title, pendingIntent).build());
    }

    private int getUiAccent() {
        int selected = getSharedPreferences("MainActivity", MODE_PRIVATE).getInt("theme", 0);
        switch (selected) {
            case 1: return 0xffffed8a; // muted lemon
            case 2: return 0xffb4eeea; // ice teal
            case 3: return 0xffffc28a; // copper amber
            case 4: return 0xffd6f27a; // Winamp green
            default: return 0xffffc247; // sodium
        }
    }

    private PendingIntent servicePending(String action, int value, int requestCode) {
        Intent intent = new Intent(this, HeadphoneService.class);
        intent.setAction(action);
        if (ACTION_SET_ANC.equals(action)) intent.putExtra(EXTRA_VALUE, value);
        return PendingIntent.getService(this, requestCode, intent, pendingFlags());
    }

    private int pendingFlags() {
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 23) flags |= PendingIntent.FLAG_IMMUTABLE;
        return flags;
    }

    private String notificationText() {
        if (connecting) return "Подключаюсь по Bluetooth SPP…";
        if (!connected) return status;
        return connectedStateText();
    }

    private String connectedStateText() {
        return "Подключено · ANC: " + (ancMode < 0 ? "неизвестен" : ANC_NAMES[ancMode])
                + " · EQ: " + (eqPreset < 0 ? "неизвестен" : EQ_NAMES[eqPreset]);
    }

    private void updateForegroundNotification() {
        if (!connected && !connecting) return;
        Notification notification = buildNotification();
        startForeground(NOTIFICATION_ID, notification);
        if (connected) updatePlaybackState();
    }

    private void publishStatus() {
        if (connected && !connecting) updateForegroundNotification();
        Intent intent = new Intent(ACTION_STATUS);
        intent.setPackage(getPackageName());
        intent.putExtra(EXTRA_STATUS, status);
        sendBroadcast(intent);
    }

    private String safeMessage(Exception error) {
        String message = error.getMessage();
        return message == null || message.length() == 0 ? "Ошибка Bluetooth-команды." : message;
    }

    private void closeSocket() {
        readerRunning = false;
        synchronized (responseMonitor) { responseMonitor.notifyAll(); }
        connected = false;
        BluetoothSocket old = socket;
        socket = null;
        input = null;
        output = null;
        readerThread = null;
        if (old != null) {
            try { old.close(); } catch (Exception ignored) { }
        }
    }
}
