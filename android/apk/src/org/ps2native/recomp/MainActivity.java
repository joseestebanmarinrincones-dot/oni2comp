package org.ps2native.recomp;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.hardware.input.InputManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.Log;
import android.view.InputDevice;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends SDLActivity {
    private static final String CARD_NAME = "ps2.ps2";
    private static final long CARD_BYTES = 8650752L;  // 16384 pages of 528 bytes
    private VirtualPadView virtualPad;
    private InputManager inputManager;
    private boolean saidStorage;
    private final Handler handler = new Handler(Looper.getMainLooper());

    // A PS3/PS4/PS5 controller (or any other gamepad Android reports) plugs into SDL as a normal controller. When one is
    // connected the on-screen pad goes away; when it is unplugged the pad comes back.
    private final InputManager.InputDeviceListener deviceListener = new InputManager.InputDeviceListener() {
        @Override
        public void onInputDeviceAdded(int deviceId) { updateControls(); }

        @Override
        public void onInputDeviceRemoved(int deviceId) { updateControls(); }

        @Override
        public void onInputDeviceChanged(int deviceId) { updateControls(); }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        prepareStorage();
        virtualPad = new VirtualPadView(this);
        addContentView(virtualPad, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (inputManager != null) inputManager.registerInputDeviceListener(deviceListener, handler);
        updateControls();
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateControls();
        // The player may have just given the all-files access the game needs; the card can be made now.
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R || Environment.isExternalStorageManager()) copyCardIfMissing();
    }

    @Override
    protected void onPause() {
        if (virtualPad != null) virtualPad.releaseAll();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        if (inputManager != null) inputManager.unregisterInputDeviceListener(deviceListener);
        super.onDestroy();
    }

    private boolean hasGamepad() {
        if (inputManager == null) return false;
        for (int id : inputManager.getInputDeviceIds()) {
            final InputDevice device = inputManager.getInputDevice(id);
            if (device == null) continue;
            final int sources = device.getSources();
            final boolean gamepad = (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD;
            final boolean joystick = (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
            final boolean touch = (sources & InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN;
            if ((gamepad || joystick) && !touch) return true;
        }
        return false;
    }

    private void updateControls() {
        if (virtualPad == null) return;
        final boolean pad = hasGamepad();
        virtualPad.setVisibility(pad ? View.GONE : View.VISIBLE);
        if (pad) virtualPad.releaseAll();
    }

    // The game and the memory card live in the phone's shared storage. The ISO is copied there by the player; the card (a
    // formatted, empty one) travels inside the APK and is put beside it the first time, so a new phone can save too.
    private void prepareStorage() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (Environment.isExternalStorageManager()) {
                copyCardIfMissing();
                return;
            }
            if (!saidStorage) {
                saidStorage = true;
                Toast.makeText(this, "Dale a PS2 Native acceso a todos los archivos para leer el juego y crear la tarjeta de memoria.", Toast.LENGTH_LONG).show();
                try {
                    startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
                } catch (Exception e) {
                    try {
                        startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                    } catch (Exception ignored) {
                    }
                }
            }
        } else {
            if (checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED)
                requestPermissions(new String[]{android.Manifest.permission.READ_EXTERNAL_STORAGE, android.Manifest.permission.WRITE_EXTERNAL_STORAGE}, 1);
            copyCardIfMissing();
        }
    }

    private void copyCardIfMissing() {
        final File card = new File(Environment.getExternalStorageDirectory(), CARD_NAME);
        if (card.isFile() && card.length() == CARD_BYTES) return;  // the player's card, saves included
        if (card.isFile() && card.length() != 0) return;            // something else: never overwrite it
        try (InputStream in = getAssets().open(CARD_NAME); OutputStream out = new FileOutputStream(card)) {
            final byte[] buffer = new byte[64 * 1024];
            int n;
            while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
        } catch (IOException e) {
            Log.w("ps2native", "cannot create the memory card " + card, e);
        }
    }

    // Called by VirtualPadView on the UI thread. libmain is loaded a moment after the activity starts (SDLActivity loads it when
    // the surface is ready), so the first calls find no library and are ignored.
    static void setVirtualPad(int buttons, float lx, float ly, float rx, float ry) {
        try {
            nativeSetVirtualPad(buttons, lx, ly, rx, ry);
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    static void toggleMenu() {
        try {
            nativeToggleMenu();
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    private static native void nativeSetVirtualPad(int buttons, float lx, float ly, float rx, float ry);

    private static native void nativeToggleMenu();

    // The image is all the game needs: the boot ELF travels inside it and the runtime reads it. A Spanish/PAL image is preferred
    // when it is there (the US SLUS release is English only); then the usual Onimusha2.iso, then any Onimusha ISO.
    private String findIso() {
        final File storage = Environment.getExternalStorageDirectory();
        final File[] files = storage.listFiles();
        if (files != null) {
            File spanish = null;
            for (File file : files) {
                final String name = file.getName().toLowerCase();
                if (!file.isFile() || !name.endsWith(".iso")) continue;
                if (name.contains("espa") || name.contains("spanish") || name.contains("spain") || name.contains("_s.") ||
                    name.contains("pal") || name.contains("sles")) {
                    spanish = file;
                    break;
                }
            }
            if (spanish != null) return spanish.getAbsolutePath();
            final File named = new File(storage, "Onimusha2.iso");
            if (named.isFile()) return named.getAbsolutePath();
            for (File file : files) {
                final String name = file.getName().toLowerCase();
                if (file.isFile() && name.contains("onimusha") && name.endsWith(".iso")) return file.getAbsolutePath();
            }
        }
        final File named = new File(storage, "Onimusha2.iso");
        Toast.makeText(this, "No encontré una ISO de Onimusha en /sdcard. Copiá Onimusha2.iso o una ISO PAL/ES a la memoria interna.", Toast.LENGTH_LONG).show();
        return named.getAbsolutePath();
    }

    @Override
    protected String[] getArguments() {
        // A test can pass a whole argument list with `am start ... --esa ps2_args ...` (and `adb` uses it to replay a run).
        final String[] extra = getIntent().getStringArrayExtra("ps2_args");
        if (extra != null && extra.length > 0) return extra;
        return new String[] {
            "--disc", findIso(),
            "--memcard", new File(Environment.getExternalStorageDirectory(), CARD_NAME).getAbsolutePath(),
            "--fullscreen"
        };
    }
}
