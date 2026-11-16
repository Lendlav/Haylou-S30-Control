package com.booka.hayloucontrol;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.media.AudioManager;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.Set;

public final class MainActivity extends Activity {
    private int SODIUM = 0xffffb300;
    private int SODIUM_BRIGHT = 0xffffc247;
    private int SODIUM_DIM = 0xff9a6810;
    private int INK = 0xff080704;
    private int PANEL = 0xff17130a;
    private int PANEL_EDGE = 0xff59400d;
    private static final String[] THEME_NAMES = {"Натрий", "Лимон", "Лёд", "Янтарь", "Winamp Classic"};
    private static final int[][] THEMES = {
            {0xffffb300, 0xffffc247, 0xff9a6810, 0xff080704, 0xff17130a, 0xff59400d},
            {0xffd7c54a, 0xffffed8a, 0xff81722b, 0xff090900, 0xff191707, 0xff5b5420},
            {0xff75c4c0, 0xffb4eeea, 0xff376e70, 0xff050a0a, 0xff121a19, 0xff315654},
            {0xffd68a4d, 0xffffc28a, 0xff794727, 0xff0a0705, 0xff1c130d, 0xff62412b},
            {0xff8fbd32, 0xffd6f27a, 0xff526f1c, 0xff101214, 0xff252a2d, 0xff596368}
    };
    private static final int REQUEST_BT_CONNECT = 41;
    private static final int PENDING_REFRESH = 1;
    private static final int PENDING_CONNECT = 2;

    private final String[] presetNames = {
            "Стандартный", "Вокал", "Рок", "Классика", "Поп", "Бас", "Саббас", "Мягкий", "Улица"
    };

    private Spinner deviceSpinner;
    private Spinner presetSpinner;
    private TextView statusView;
    private TextView ancTitleView;
    private int currentAncMode = -1;
    private TextView volumeValue;
    private SeekBar volumeBar;
    private AudioManager audioManager;
    private ArrayAdapter<DeviceEntry> deviceAdapter;
    private final List<DeviceEntry> devices = new ArrayList<DeviceEntry>();
    private boolean receiverRegistered;
    private boolean autoConnectAttempted;
    private int pendingPermissionAction;

    private final BroadcastReceiver serviceReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (HeadphoneService.ACTION_STATUS.equals(intent.getAction())) {
                String status = intent.getStringExtra(HeadphoneService.EXTRA_STATUS);
                if (status != null) {
                    statusView.setText(status);
                    updateAncIndicator(status);
                }
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        audioManager = (AudioManager) getSystemService(AUDIO_SERVICE);
        applySavedTheme();
        buildInterface();
        requestStateFromRunningService();
        refreshDevices();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!receiverRegistered) {
            IntentFilter filter = new IntentFilter(HeadphoneService.ACTION_STATUS);
            registerReceiver(serviceReceiver, filter);
            receiverRegistered = true;
        }
        updateVolumeFromSystem();
        requestStateFromRunningService();
    }

    @Override
    protected void onPause() {
        if (receiverRegistered) {
            unregisterReceiver(serviceReceiver);
            receiverRegistered = false;
        }
        super.onPause();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQUEST_BT_CONNECT) return;
        boolean granted = grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED;
        if (!granted) {
            statusView.setText("Разрешите доступ «Устройства поблизости» для Bluetooth-подключения.");
            return;
        }
        int action = pendingPermissionAction;
        pendingPermissionAction = 0;
        if (action == PENDING_CONNECT) connectSelectedDevice();
        else refreshDevices();
    }

    private void buildInterface() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(16), dp(12), dp(16), dp(18));
        page.setBackgroundColor(INK);

        LinearLayout topBar = new LinearLayout(this);
        topBar.setGravity(Gravity.CENTER_VERTICAL);
        TextView heading = text("HAYLOU S30", 23, SODIUM_BRIGHT, true);
        heading.setShadowLayer(dp(11), 0, 0, SODIUM_DIM);
        topBar.addView(heading, new LinearLayout.LayoutParams(0, -2, 1));
        Button settings = button("⚙");
        settings.setTextSize(22);
        topBar.addView(settings, new LinearLayout.LayoutParams(dp(48), dp(44)));
        settings.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { showThemeSettings(); }
        });
        page.addView(topBar, matchWrap());
        TextView intro = text("Управление наушниками без лишних служб и библиотек", 13, 0xffc49b50, false);
        intro.setPadding(0, dp(2), 0, dp(10));
        page.addView(intro, matchWrap());
        LinearLayout deviceCard = section(page, "Наушники");
        deviceSpinner = new Spinner(this);
        deviceAdapter = new ArrayAdapter<DeviceEntry>(this, android.R.layout.simple_spinner_item, devices) {
            @Override public View getView(int position, View convertView, android.view.ViewGroup parent) {
                TextView item = (TextView) super.getView(position, convertView, parent);
                styleSpinnerText(item);
                return item;
            }
            @Override public View getDropDownView(int position, View convertView, android.view.ViewGroup parent) {
                TextView item = (TextView) super.getDropDownView(position, convertView, parent);
                styleSpinnerText(item);
                return item;
            }
        };
        deviceSpinner.setAdapter(deviceAdapter);
        styleSpinner(deviceSpinner);
        LinearLayout.LayoutParams spinnerParams = new LinearLayout.LayoutParams(-1, dp(48));
        deviceCard.addView(deviceSpinner, spinnerParams);

        LinearLayout deviceButtons = row(deviceCard);
        Button refreshButton = button("Обновить");
        refreshButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { withBluetoothPermission(PENDING_REFRESH); }
        });
        Button connectButton = button("Подключить");
        connectButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { withBluetoothPermission(PENDING_CONNECT); }
        });
        Button disconnectButton = button("Отключить");
        disconnectButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { sendServiceAction(HeadphoneService.ACTION_DISCONNECT); }
        });
        addWeighted(deviceButtons, refreshButton);
        addWeighted(deviceButtons, connectButton);
        addWeighted(deviceButtons, disconnectButton);

        LinearLayout ancCard = section(page, "ANC");
        ancTitleView = (TextView) ancCard.getChildAt(0);
        applyAncGlow();
        LinearLayout ancRow = row(ancCard);
        addWeighted(ancRow, commandButton("Выкл.", HeadphoneService.ACTION_SET_ANC, 0));
        addWeighted(ancRow, commandButton("Шумопод.", HeadphoneService.ACTION_SET_ANC, 1));
        addWeighted(ancRow, commandButton("Прозрачность", HeadphoneService.ACTION_SET_ANC, 2));

        LinearLayout eqCard = section(page, "Штатный эквалайзер");
        presetSpinner = new Spinner(this);
        ArrayAdapter<String> presetAdapter = new ArrayAdapter<String>(this,
                android.R.layout.simple_spinner_item, presetNames) {
            @Override public View getView(int position, View convertView, android.view.ViewGroup parent) {
                TextView item = (TextView) super.getView(position, convertView, parent);
                styleSpinnerText(item);
                return item;
            }
            @Override public View getDropDownView(int position, View convertView, android.view.ViewGroup parent) {
                TextView item = (TextView) super.getDropDownView(position, convertView, parent);
                styleSpinnerText(item);
                return item;
            }
        };
        presetSpinner.setAdapter(presetAdapter);
        styleSpinner(presetSpinner);
        eqCard.addView(presetSpinner, new LinearLayout.LayoutParams(-1, dp(48)));
        Button applyPresetButton = button("Применить пресет");
        applyPresetButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) {
                Intent intent = new Intent(MainActivity.this, HeadphoneService.class);
                intent.setAction(HeadphoneService.ACTION_SET_EQ);
                intent.putExtra(HeadphoneService.EXTRA_VALUE, presetSpinner.getSelectedItemPosition());
                startServiceCompat(intent);
            }
        });
        eqCard.addView(applyPresetButton, new LinearLayout.LayoutParams(-1, dp(48)));

        LinearLayout volumeCard = section(page, "Громкость Bluetooth-аудио");
        LinearLayout volumeRow = row(volumeCard);
        volumeBar = new SeekBar(this);
        volumeBar.setMax(audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC));
        volumeBar.setProgress(audioManager.getStreamVolume(AudioManager.STREAM_MUSIC));
        volumeValue = text("", 14, SODIUM_BRIGHT, false);
        volumeValue.setGravity(Gravity.CENTER_VERTICAL | Gravity.RIGHT);
        volumeValue.setMinWidth(dp(42));
        volumeRow.addView(volumeBar, new LinearLayout.LayoutParams(0, dp(48), 1));
        volumeRow.addView(volumeValue, new LinearLayout.LayoutParams(dp(48), dp(48)));
        volumeBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                updateVolumeLabel(progress);
                if (fromUser) audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, progress, 0);
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) { }
            @Override public void onStopTrackingTouch(SeekBar seekBar) { }
        });
        TextView volumeHint = text("Меняет системную громкость мультимедиа. Работают и кнопки громкости телефона.",
                12, 0xffc49b50, false);
        volumeHint.setPadding(0, dp(2), 0, 0);
        volumeCard.addView(volumeHint, matchWrap());

        statusView = text("Готово. Сопрягите HAYLOU S30 в настройках Bluetooth.", 13, SODIUM_BRIGHT, false);
        statusView.setPadding(dp(2), dp(8), dp(2), dp(8));
        page.addView(statusView, matchWrap());
        TextView lockHint = text("При подключении появится карточка управления на экране блокировки. Для её показа разрешите уведомления и медиаконтролы на экране блокировки в настройках Android.",
                12, 0xff80652f, false);
        lockHint.setPadding(dp(2), dp(4), dp(2), 0);
        page.addView(lockHint, matchWrap());

        scroll.addView(page);
        setContentView(scroll);
    }

    private void showThemeSettings() {
        final int selectedTheme = getPreferences(MODE_PRIVATE).getInt("theme", 0);
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(18), dp(16), dp(18), dp(22));
        page.setBackgroundColor(INK);
        LinearLayout bar = new LinearLayout(this);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        Button back = button("‹");
        back.setTextSize(30);
        bar.addView(back, new LinearLayout.LayoutParams(dp(48), dp(48)));
        LinearLayout titles = new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        titles.setPadding(dp(12), 0, 0, 0);
        TextView title = text("Оформление", 22, SODIUM_BRIGHT, true);
        title.setShadowLayer(dp(8), 0, 0, SODIUM_DIM);
        TextView subTitle = text("ВЫБЕРИТЕ СВОЮ ПАЛИТРУ", 10, 0xff9c8b69, true);
        titles.addView(title, matchWrap());
        titles.addView(subTitle, matchWrap());
        bar.addView(titles, new LinearLayout.LayoutParams(0, -2, 1));
        TextView active = text(String.format(Locale.ROOT, "%02d", selectedTheme + 1), 13, SODIUM_BRIGHT, true);
        active.setGravity(Gravity.CENTER);
        active.setBackground(roundRect(PANEL, dp(18)));
        bar.addView(active, new LinearLayout.LayoutParams(dp(38), dp(38)));
        page.addView(bar, matchWrap());
        back.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { buildInterface(); }
        });
        TextView hint = text("Подсветка и фон интерфейса", 14, 0xffb7a783, false);
        hint.setPadding(dp(2), dp(20), 0, dp(12));
        page.addView(hint, matchWrap());
        for (int i = 0; i < THEME_NAMES.length; i++) {
            final int index = i;
            final int[] palette = THEMES[i];
            boolean isSelected = selectedTheme == i;
            LinearLayout card = new LinearLayout(this);
            card.setGravity(Gravity.CENTER_VERTICAL);
            card.setPadding(dp(12), dp(10), dp(12), dp(10));
            GradientDrawable cardBg = roundRect(isSelected ? blend(palette[3], palette[2], 0.24f) : palette[4], dp(11));
            cardBg.setStroke(dp(isSelected ? 2 : 1), isSelected ? palette[0] : palette[5]);
            card.setBackground(cardBg);
            card.setClickable(true);
            card.setFocusable(true);
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, dp(76));
            params.bottomMargin = dp(10);
            page.addView(card, params);

            View swatch = new View(this);
            GradientDrawable swatchBg = roundRect(palette[3], dp(9));
            swatchBg.setStroke(dp(1), palette[5]);
            swatch.setBackground(swatchBg);
            LinearLayout.LayoutParams swatchParams = new LinearLayout.LayoutParams(dp(48), dp(48));
            swatchParams.rightMargin = dp(13);
            card.addView(swatch, swatchParams);
            View glow = new View(this);
            glow.setBackground(roundRect(palette[1], dp(3)));
            LinearLayout.LayoutParams glowParams = new LinearLayout.LayoutParams(dp(25), dp(5));
            glowParams.gravity = Gravity.CENTER_VERTICAL;
            glowParams.leftMargin = -dp(49);
            glowParams.rightMargin = dp(24);
            card.addView(glow, glowParams);

            LinearLayout labels = new LinearLayout(this);
            labels.setOrientation(LinearLayout.VERTICAL);
            TextView name = text(THEME_NAMES[i], 15, palette[1], true);
            name.setShadowLayer(dp(5), 0, 0, palette[2]);
            TextView detail = text(themeDescription(i), 11, 0xffb2a58a, false);
            labels.addView(name, matchWrap());
            labels.addView(detail, matchWrap());
            card.addView(labels, new LinearLayout.LayoutParams(0, -2, 1));

            TextView mark = text(isSelected ? "✓" : "›", isSelected ? 19 : 24, palette[1], true);
            mark.setGravity(Gravity.CENTER);
            card.addView(mark, new LinearLayout.LayoutParams(dp(30), dp(40)));
            card.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    getPreferences(MODE_PRIVATE).edit().putInt("theme", index).apply();
                    applyTheme(index);
                    showThemeSettings();
                }
            });
        }
        View divider = new View(this);
        divider.setBackgroundColor(PANEL_EDGE);
        LinearLayout.LayoutParams dividerParams = new LinearLayout.LayoutParams(-1, dp(1));
        dividerParams.topMargin = dp(8);
        dividerParams.bottomMargin = dp(12);
        page.addView(divider, dividerParams);
        String version = "S30  ·  версия " + getVersionName();
        TextView versionView = text(version, 11, 0xff94866b, false);
        versionView.setGravity(Gravity.CENTER);
        versionView.setPadding(0, dp(2), 0, dp(4));
        page.addView(versionView, matchWrap());
        setContentView(page);
    }

    private String getVersionName() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionName; }
        catch (PackageManager.NameNotFoundException ignored) { return "—"; }
    }

    private String themeDescription(int index) {
        switch (index) {
            case 1: return "Тёмный графит · мягкий лимон";
            case 2: return "Глубокий сине-зелёный · ледяной свет";
            case 3: return "Тёмный шоколад · медный янтарь";
            case 4: return "Графит · классический зелёный";
            default: return "Почти чёрный · натриевая лампа";
        }
    }

    private void applySavedTheme() { applyTheme(getPreferences(MODE_PRIVATE).getInt("theme", 0)); }

    private void applyTheme(int index) {
        int[] t = THEMES[Math.max(0, Math.min(THEMES.length - 1, index))];
        SODIUM=t[0]; SODIUM_BRIGHT=t[1]; SODIUM_DIM=t[2]; INK=t[3]; PANEL=t[4]; PANEL_EDGE=t[5];
        getWindow().setStatusBarColor(INK);
        getWindow().setNavigationBarColor(INK);
    }

    private Button commandButton(String label, final String action, final int value) {
        Button result = button(label);
        result.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) {
                Intent intent = new Intent(MainActivity.this, HeadphoneService.class);
                intent.setAction(action);
                intent.putExtra(HeadphoneService.EXTRA_VALUE, value);
                startServiceCompat(intent);
            }
        });
        return result;
    }

    private void withBluetoothPermission(int action) {
        if (Build.VERSION.SDK_INT >= 31 && checkSelfPermission("android.permission.BLUETOOTH_CONNECT")
                != PackageManager.PERMISSION_GRANTED) {
            pendingPermissionAction = action;
            requestPermissions(new String[]{"android.permission.BLUETOOTH_CONNECT"}, REQUEST_BT_CONNECT);
            return;
        }
        if (action == PENDING_CONNECT) connectSelectedDevice();
        else refreshDevices();
    }

    private void refreshDevices() {
        BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        devices.clear();
        if (adapter == null) {
            deviceAdapter.notifyDataSetChanged();
            statusView.setText("В этом телефоне нет Bluetooth.");
            return;
        }
        try {
            if (!adapter.isEnabled()) {
                deviceAdapter.notifyDataSetChanged();
                statusView.setText("Включите Bluetooth и сопрягите HAYLOU S30 в настройках телефона.");
                return;
            }
            Set<BluetoothDevice> paired = adapter.getBondedDevices();
            if (paired != null) {
                for (BluetoothDevice device : paired) {
                    String name = device.getName();
                    if (name == null) continue;
                    String normalized = name.toLowerCase(Locale.ROOT);
                    if (normalized.contains("haylou") && normalized.contains("s30")) {
                        devices.add(new DeviceEntry(name, device.getAddress()));
                    }
                }
            }
            Collections.sort(devices, new Comparator<DeviceEntry>() {
                @Override public int compare(DeviceEntry left, DeviceEntry right) {
                    return left.name.compareToIgnoreCase(right.name);
                }
            });
            deviceAdapter.notifyDataSetChanged();
            String preferredAddress = getPreferences(MODE_PRIVATE).getString("device_address", null);
            if (preferredAddress != null) {
                for (int i = 0; i < devices.size(); i++) {
                    if (preferredAddress.equals(devices.get(i).address)) {
                        deviceSpinner.setSelection(i);
                        if (!autoConnectAttempted) {
                            autoConnectAttempted = true;
                            connectSelectedDevice();
                        }
                        break;
                    }
                }
            }
            if (devices.isEmpty()) statusView.setText("Сопряжённый HAYLOU S30 не найден. Выполните сопряжение в настройках Bluetooth.");
            else statusView.setText("Найдено сопряжённых HAYLOU S30: " + devices.size());
        } catch (SecurityException error) {
            statusView.setText("Android не разрешил доступ к сопряжённым Bluetooth-устройствам.");
        }
    }

    private void connectSelectedDevice() {
        if (deviceSpinner.getSelectedItemPosition() < 0 || devices.isEmpty()) {
            statusView.setText("Сначала сопрягите HAYLOU S30 и обновите список.");
            return;
        }
        DeviceEntry device = devices.get(deviceSpinner.getSelectedItemPosition());
        getPreferences(MODE_PRIVATE).edit().putString("device_address", device.address).apply();
        Intent intent = new Intent(this, HeadphoneService.class);
        intent.setAction(HeadphoneService.ACTION_CONNECT);
        intent.putExtra(HeadphoneService.EXTRA_ADDRESS, device.address);
        intent.putExtra(HeadphoneService.EXTRA_NAME, device.name);
        statusView.setText("Подключаюсь к " + device.name + " по SPP…");
        startForegroundServiceCompat(intent);
    }

    private void requestStateFromRunningService() {
        Intent intent = new Intent(this, HeadphoneService.class);
        intent.setAction(HeadphoneService.ACTION_STATE);
        startServiceCompat(intent);
    }

    private void sendServiceAction(String action) {
        Intent intent = new Intent(this, HeadphoneService.class);
        intent.setAction(action);
        startServiceCompat(intent);
    }

    private void startForegroundServiceCompat(Intent intent) {
        if (Build.VERSION.SDK_INT >= 26) {
            try {
                getClass().getMethod("startForegroundService", Intent.class).invoke(this, intent);
                return;
            } catch (Exception ignored) { }
        }
        startService(intent);
    }

    private void startServiceCompat(Intent intent) {
        startService(intent);
    }

    private void updateVolumeFromSystem() {
        if (volumeBar == null || audioManager == null) return;
        int max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
        volumeBar.setMax(max);
        volumeBar.setProgress(audioManager.getStreamVolume(AudioManager.STREAM_MUSIC));
    }

    private void updateVolumeLabel(int value) {
        if (volumeValue == null) return;
        int max = Math.max(1, audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC));
        volumeValue.setText((value * 100 / max) + "%");
    }

    private LinearLayout section(LinearLayout parent, String title) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(12), dp(8), dp(12), dp(10));
        box.setBackground(panelBackground());
        LinearLayout.LayoutParams boxParams = new LinearLayout.LayoutParams(-1, -2);
        boxParams.bottomMargin = dp(10);
        parent.addView(box, boxParams);
        TextView titleView = text(title, 14, SODIUM_BRIGHT, true);
        titleView.setShadowLayer(dp(4), 0, 0, 0xff65430a);
        titleView.setPadding(0, 0, 0, dp(5));
        box.addView(titleView, matchWrap());
        return box;
    }

    private LinearLayout row(LinearLayout parent) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        parent.addView(row, matchWrap());
        return row;
    }

    private void addWeighted(LinearLayout row, View child) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, dp(46), 1);
        params.leftMargin = dp(3);
        params.rightMargin = dp(3);
        row.addView(child, params);
    }

    private Button button(String label) {
        Button result = new Button(this);
        result.setText(label);
        result.setTextColor(SODIUM_BRIGHT);
        result.setTextSize(12);
        result.setAllCaps(false);
        result.setMinHeight(dp(44));
        StateListDrawable states = new StateListDrawable();
        states.addState(new int[]{android.R.attr.state_pressed}, roundRect(SODIUM_BRIGHT, dp(7)));
        states.addState(new int[]{android.R.attr.state_focused}, roundRect(PANEL_EDGE, dp(7)));
        states.addState(new int[]{-android.R.attr.state_enabled}, roundRect(PANEL, dp(7)));
        states.addState(new int[]{}, roundRect(blend(INK, SODIUM_DIM, 0.16f), dp(7)));
        result.setOnTouchListener(new View.OnTouchListener() {
            @Override public boolean onTouch(View view, android.view.MotionEvent event) {
                if (event.getAction() == android.view.MotionEvent.ACTION_DOWN) {
                    view.setElevation(dp(5));
                    view.setTranslationY(-dp(1));
                } else if (event.getAction() == android.view.MotionEvent.ACTION_UP
                        || event.getAction() == android.view.MotionEvent.ACTION_CANCEL) {
                    view.setElevation(0);
                    view.setTranslationY(0);
                }
                return false;
            }
        });
        result.setBackground(states);
        return result;
    }

    private void updateAncIndicator(String status) {
        if (ancTitleView == null) return;
        if (status.contains("ANC: Шумоподавление")) currentAncMode = 1;
        else if (status.contains("ANC: Прозрачность")) currentAncMode = 2;
        else if (status.contains("ANC: Выкл.")) currentAncMode = 0;
        else return;
        getPreferences(MODE_PRIVATE).edit().putInt("anc_mode", currentAncMode).apply();
        applyAncGlow();
    }

    private void applyAncGlow() {
        if (ancTitleView == null) return;
        if (currentAncMode < 0) currentAncMode = getPreferences(MODE_PRIVATE).getInt("anc_mode", -1);
        if (currentAncMode < 0) return;
        int color = currentAncMode == 1 ? 0xff00e676 : currentAncMode == 2 ? 0xffffd740 : 0xffff5252;
        ancTitleView.setTextColor(color);
        ancTitleView.setShadowLayer(dp(9), 0, 0, color);
    }

    private void styleSpinner(Spinner spinner) {
        spinner.setBackground(roundRect(0xff100d07, dp(6)));
        spinner.setPopupBackgroundDrawable(roundRect(PANEL, dp(6)));
        spinner.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(android.widget.AdapterView<?> parent, View view, int position, long id) {
                if (view instanceof TextView) ((TextView) view).setTextColor(SODIUM_BRIGHT);
            }
            @Override public void onNothingSelected(android.widget.AdapterView<?> parent) { }
        });
    }

    private void styleSpinnerText(TextView item) {
        item.setTextColor(SODIUM_BRIGHT);
        item.setTextSize(14);
        item.setPadding(dp(10), dp(8), dp(10), dp(8));
        item.setBackgroundColor(PANEL);
    }

    private GradientDrawable panelBackground() {
        GradientDrawable background = roundRect(PANEL, dp(9));
        background.setStroke(dp(1), PANEL_EDGE);
        return background;
    }

    private int blend(int base, int accent, float amount) {
        int r = (int) (Color.red(base) * (1f - amount) + Color.red(accent) * amount);
        int g = (int) (Color.green(base) * (1f - amount) + Color.green(accent) * amount);
        int b = (int) (Color.blue(base) * (1f - amount) + Color.blue(accent) * amount);
        return Color.rgb(r, g, b);
    }

    private TextView text(String value, float size, int color, boolean bold) {
        TextView result = new TextView(this);
        result.setText(value);
        result.setTextSize(size);
        result.setTextColor(color);
        if (bold) result.setTypeface(null, android.graphics.Typeface.BOLD);
        return result;
    }

    private GradientDrawable roundRect(int color, int radius) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(color);
        shape.setCornerRadius(radius);
        return shape;
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(-1, -2);
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private static final class DeviceEntry {
        final String name;
        final String address;
        DeviceEntry(String name, String address) { this.name = name; this.address = address; }
        @Override public String toString() { return name; }
    }
}
