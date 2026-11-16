package com.booka.hayloucontrol;

import android.bluetooth.BluetoothDevice;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

/** Reopens the Haylou S30 SPP control channel when the paired headset reconnects. */
public final class BluetoothReconnectReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        if (!BluetoothDevice.ACTION_ACL_CONNECTED.equals(intent.getAction())) return;
        BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
        if (device == null) return;
        String name;
        try { name = device.getName(); } catch (SecurityException error) { return; }
        if (name == null) return;
        String normalized = name.toLowerCase(java.util.Locale.ROOT);
        if (!normalized.contains("haylou") || !normalized.contains("s30")) return;
        String preferred = context.getSharedPreferences("com.booka.hayloucontrol_preferences", Context.MODE_PRIVATE)
                .getString("device_address", null);
        if (preferred != null && !preferred.equals(device.getAddress())) return;
        Intent connect = new Intent(context, HeadphoneService.class)
                .setAction(HeadphoneService.ACTION_CONNECT)
                .putExtra(HeadphoneService.EXTRA_ADDRESS, device.getAddress())
                .putExtra(HeadphoneService.EXTRA_NAME, name);
        if (Build.VERSION.SDK_INT >= 26) context.startForegroundService(connect);
        else context.startService(connect);
    }
}
