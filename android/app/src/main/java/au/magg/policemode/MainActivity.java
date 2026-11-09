package au.magg.policemode;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import com.hoho.android.usbserial.driver.UsbSerialDriver;
import com.hoho.android.usbserial.driver.UsbSerialPort;
import com.hoho.android.usbserial.driver.UsbSerialProber;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

public final class MainActivity extends Activity {
    static {
        System.loadLibrary("pme");
    }

    private static final String USB_PERMISSION = "au.magg.policemode.USB_PERMISSION";
    private static final int[] BAUD_RATES = {9600, 19200, 38400, 57600, 115200};

    private final List<UsbSerialDriver> drivers = new ArrayList<>();
    private final AtomicBoolean busy = new AtomicBoolean(false);
    private UsbManager usbManager;
    private Spinner deviceList;
    private Spinner speedList;
    private CheckBox untested;
    private TextView status;
    private UsbSerialDriver pendingDriver;
    private int pendingMode;
    private int pendingBaud;
    private boolean pendingUntested;

    private native String nativeRun(UsbSerialPort port, int operation, boolean allowUntested);

    private final BroadcastReceiver permissionReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!USB_PERMISSION.equals(intent.getAction())) return;
            UsbSerialDriver driver = pendingDriver;
            pendingDriver = null;
            if (driver == null) return;
            UsbDevice device = driver.getDevice();
            if (usbManager.hasPermission(device)) runOperation(driver, pendingMode, pendingBaud, pendingUntested);
            else finishOperation("USB permission denied.");
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(Color.BLACK);
        getWindow().setNavigationBarColor(Color.BLACK);
        usbManager = (UsbManager) getSystemService(Context.USB_SERVICE);
        IntentFilter filter = new IntentFilter(USB_PERMISSION);
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(permissionReceiver, filter, Context.RECEIVER_EXPORTED);
        else registerReceiver(permissionReceiver, filter);
        buildInterface();
        refreshDevices();
    }

    private TextView label(String value, int size, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextColor(color);
        view.setTextSize(size);
        view.setGravity(Gravity.CENTER);
        return view;
    }

    private LinearLayout.LayoutParams spacing(int top, int bottom) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(top);
        params.bottomMargin = dp(bottom);
        return params;
    }

    private int dp(int size) {
        return Math.round(getResources().getDisplayMetrics().density * size);
    }

    private Button button(String title, LinearLayout layout, int top) {
        Button control = new Button(this);
        control.setText(title);
        control.setAllCaps(false);
        layout.addView(control, spacing(top, 0));
        return control;
    }

    private void buildInterface() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(Color.BLACK);
        LinearLayout outer = new LinearLayout(this);
        outer.setGravity(Gravity.CENTER);
        outer.setOrientation(LinearLayout.VERTICAL);
        outer.setPadding(dp(20), dp(28), dp(20), dp(28));
        scroll.addView(outer);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setGravity(Gravity.CENTER_HORIZONTAL);
        outer.addView(content, new LinearLayout.LayoutParams(
            Math.min(dp(380), getResources().getDisplayMetrics().widthPixels - dp(40)),
            LinearLayout.LayoutParams.WRAP_CONTENT));

        content.addView(label("Falcon Police Mode", 23, Color.WHITE), spacing(0, 10));
        content.addView(label("BF 5R29 tested", 14, Color.LTGRAY), spacing(0, 3));
        content.addView(label("BA Falcon / SX-SY Territory untested", 12, Color.GRAY), spacing(0, 22));

        content.addView(label("USB serial adapter", 14, Color.WHITE), spacing(0, 6));
        deviceList = new Spinner(this);
        content.addView(deviceList, spacing(0, 9));
        content.addView(label("Serial speed", 14, Color.WHITE), spacing(0, 6));
        speedList = new Spinner(this);
        String[] rates = new String[BAUD_RATES.length];
        for (int i = 0; i < BAUD_RATES.length; i++) rates[i] = BAUD_RATES[i] + " baud";
        ArrayAdapter<String> baudAdapter = new ArrayAdapter<>(this,
            android.R.layout.simple_spinner_dropdown_item, rates);
        speedList.setAdapter(baudAdapter);
        speedList.setSelection(2);
        content.addView(speedList, spacing(0, 8));

        Button refresh = button("Refresh USB devices", content, 5);
        refresh.setOnClickListener(view -> refreshDevices());
        Button read = button("Read status", content, 15);
        read.setOnClickListener(view -> requestOperation(0));
        Button enable = button("Enable Police Mode", content, 8);
        enable.setOnClickListener(view -> requestOperation(1));
        Button disable = button("Disable Police Mode", content, 8);
        disable.setOnClickListener(view -> requestOperation(2));

        untested = new CheckBox(this);
        untested.setText("Allow unverified cluster versions");
        untested.setTextColor(Color.LTGRAY);
        untested.setTextSize(13);
        content.addView(untested, spacing(12, 12));

        status = label("Connect an ELM327 USB adapter.", 14, Color.WHITE);
        content.addView(status, spacing(8, 14));
        content.addView(label("Configuration writes require confirmation and readback verification.",
            12, Color.GRAY), spacing(0, 0));
        setContentView(scroll);
    }

    private void refreshDevices() {
        drivers.clear();
        drivers.addAll(UsbSerialProber.getDefaultProber().findAllDrivers(usbManager));
        List<String> names = new ArrayList<>();
        for (UsbSerialDriver driver : drivers) {
            UsbDevice device = driver.getDevice();
            names.add(String.format("%04X:%04X  -  %s", device.getVendorId(),
                device.getProductId(), device.getDeviceName()));
        }
        if (names.isEmpty()) names.add("No supported USB adapter");
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this,
            android.R.layout.simple_spinner_dropdown_item, names);
        deviceList.setAdapter(adapter);
    }

    private void requestOperation(int mode) {
        if (!busy.compareAndSet(false, true)) return;
        int selected = deviceList.getSelectedItemPosition();
        if (selected < 0 || selected >= drivers.size()) {
            finishOperation("Connect your CH340 ELM327 adapter via USB-C and refresh.");
            return;
        }
        UsbSerialDriver driver = drivers.get(selected);
        int baud = BAUD_RATES[speedList.getSelectedItemPosition()];
        boolean allow = untested.isChecked();
        if (mode == 0) {
            acquireUsb(driver, mode, baud, allow);
            return;
        }
        String label = mode == 1 ? "enable" : "disable";
        String warning = "This will " + label + " Police Mode by changing cluster configuration bytes. "
            + "Only BF firmware 5R29 has been tested. Other clusters may behave differently. "
            + "Do not disconnect the adapter while writing.";
        new AlertDialog.Builder(this)
            .setTitle("Confirm configuration change")
            .setMessage(warning)
            .setNegativeButton("Cancel", (dialog, which) -> busy.set(false))
            .setPositiveButton(mode == 1 ? "Enable" : "Disable", (dialog, which) ->
                acquireUsb(driver, mode, baud, allow))
            .setOnCancelListener(dialog -> busy.set(false))
            .show();
    }

    private void acquireUsb(UsbSerialDriver driver, int mode, int baud, boolean allow) {
        UsbDevice device = driver.getDevice();
        if (usbManager.hasPermission(device)) {
            runOperation(driver, mode, baud, allow);
            return;
        }
        pendingDriver = driver;
        pendingMode = mode;
        pendingBaud = baud;
        pendingUntested = allow;
        Intent intent = new Intent(USB_PERMISSION).setPackage(getPackageName());
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 31) flags |= PendingIntent.FLAG_MUTABLE;
        else flags |= PendingIntent.FLAG_IMMUTABLE;
        PendingIntent permission = PendingIntent.getBroadcast(this, 0, intent, flags);
        usbManager.requestPermission(device, permission);
        status.setText("Waiting for USB permission...");
    }

    private void runOperation(UsbSerialDriver driver, int mode, int baud, boolean allow) {
        status.setText("Communicating with cluster...");
        new Thread(() -> {
            UsbDeviceConnection connection = usbManager.openDevice(driver.getDevice());
            if (connection == null) {
                finishOperation("Could not open USB adapter. Check USB permissions.");
                return;
            }
            UsbSerialPort port = driver.getPorts().get(0);
            String result;
            try {
                port.open(connection);
                port.setParameters(baud, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE);
                result = nativeRun(port, mode, allow);
            } catch (IOException | RuntimeException error) {
                result = "USB error: " + error.getMessage();
            } finally {
                try { port.close(); } catch (IOException ignored) { }
                connection.close();
            }
            finishOperation(result);
        }).start();
    }

    private void finishOperation(String result) {
        runOnUiThread(() -> {
            status.setText(result);
            busy.set(false);
        });
    }

    @Override
    protected void onDestroy() {
        unregisterReceiver(permissionReceiver);
        super.onDestroy();
    }
}
