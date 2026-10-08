package io.github.wootbeer.btgarecomp;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * Hosts the native game (libmain.so, built from the repo-root CMakeLists.txt) through SDL.
 */
public class BattleTanxActivity extends SDLActivity {
    private static final String TAG = "BTGA";

    // Input diagnostics (the built-in controls don't reach SDL on the Retroid Pocket 6): the
    // devices Android lists and the first input events the activity receives, before SDL.
    private static final int MAX_LOGGED_EVENTS = 40;
    private int loggedEvents = 0;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        installAssets();
        logInputDevices();
        super.onCreate(savedInstanceState);
    }

    private static void logInputDevices() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device != null) {
                Log.i(TAG, "Input device " + id + ": " + device.getName()
                        + " sources=0x" + Integer.toHexString(device.getSources())
                        + (device.isVirtual() ? " virtual" : "")
                        + (device.isExternal() ? " external" : ""));
            }
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (loggedEvents < MAX_LOGGED_EVENTS && event.getAction() == KeyEvent.ACTION_DOWN) {
            loggedEvents++;
            Log.i(TAG, "Key event: " + KeyEvent.keyCodeToString(event.getKeyCode())
                    + " device=" + event.getDeviceId()
                    + " source=0x" + Integer.toHexString(event.getSource()));
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (loggedEvents < MAX_LOGGED_EVENTS) {
            loggedEvents++;
            Log.i(TAG, "Motion event: action=" + event.getActionMasked()
                    + " device=" + event.getDeviceId()
                    + " source=0x" + Integer.toHexString(event.getSource()));
        }
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    // src/main/main.cpp keeps its own main() (SDL_MAIN_HANDLED) rather than SDL_main.
    @Override
    protected String getMainFunction() {
        return "main";
    }

    /**
     * The game reads its fonts, icons and stylesheet from assets/ in its working directory,
     * which on Android is the app's private storage (see src/android/android_startup.cpp).
     * Copies the repo's assets/ folder, packaged as the APK's assets, there whenever the app
     * was installed or updated since the last copy.
     */
    private void installAssets() {
        File target = new File(getFilesDir(), "assets");
        File stamp = new File(getFilesDir(), "assets.stamp");
        String installed;
        try {
            installed = Long.toString(getPackageManager()
                    .getPackageInfo(getPackageName(), 0).lastUpdateTime);
        } catch (Exception e) {
            installed = "unknown";
        }
        if (installed.equals(readText(stamp)) && target.isDirectory()) {
            return;
        }

        try {
            deleteRecursively(target);
            copyAssetTree(getAssets(), "", target);
            writeText(stamp, installed);
        } catch (IOException e) {
            Log.e(TAG, "Couldn't copy the game's assets", e);
        }
    }

    private static void copyAssetTree(AssetManager assets, String path, File target) throws IOException {
        String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            // A file (or an empty folder, which the packager drops anyway).
            try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
                byte[] buf = new byte[64 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
            }
            return;
        }
        if (!target.isDirectory() && !target.mkdirs()) {
            throw new IOException("Couldn't create " + target);
        }
        for (String child : children) {
            // Folders the system adds to every APK's assets.
            if (path.isEmpty() && (child.equals("images") || child.equals("webkit"))) {
                continue;
            }
            copyAssetTree(assets, path.isEmpty() ? child : path + "/" + child, new File(target, child));
        }
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteRecursively(child);
            }
        }
        file.delete();
    }

    private static String readText(File file) {
        try {
            return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
        } catch (IOException e) {
            return null;
        }
    }

    private static void writeText(File file, String text) throws IOException {
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
}
