package io.github.wootbeer.btgarecomp;

import android.content.Context;
import android.content.res.AssetManager;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewGroup;

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
public class BattleTanxActivity extends SDLActivity implements InputManager.InputDeviceListener {
    private static final String TAG = "BTGA";

    private TouchControlsView touchControls;
    private GameMenu menu;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        installAssets();
        super.onCreate(savedInstanceState);

        // SDL's layout holds the game's surface; the touch controls go over it.
        if (mLayout != null && !mBrokenLibraries) {
            touchControls = new TouchControlsView(this);
            mLayout.addView(touchControls, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            menu = new GameMenu(this, touchControls);
            touchControls.setMenuListener(menu::show);

            InputManager inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
            if (inputManager != null) {
                inputManager.registerInputDeviceListener(this, null);
            }
            touchControls.setGamepadInUse(anyGamepadConnected());
        }
    }

    @Override
    protected void onDestroy() {
        InputManager inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (inputManager != null && touchControls != null) {
            inputManager.unregisterInputDeviceListener(this);
        }
        super.onDestroy();
    }

    // Back (and a gamepad's menu button) opens the app's menu instead of closing the game.
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int keyCode = event.getKeyCode();
        if (menu != null && (keyCode == KeyEvent.KEYCODE_BACK || keyCode == KeyEvent.KEYCODE_MENU)) {
            if (event.getAction() == KeyEvent.ACTION_UP) {
                menu.show();
            }
            return true;
        }
        if (touchControls != null && isGamepadEvent(event.getSource()) && event.getAction() == KeyEvent.ACTION_DOWN) {
            touchControls.setGamepadInUse(true);
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (touchControls != null && isGamepadEvent(event.getSource()) && event.getAction() == MotionEvent.ACTION_MOVE) {
            for (int axis : new int[] { MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z,
                    MotionEvent.AXIS_RZ, MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y }) {
                if (Math.abs(event.getAxisValue(axis)) > 0.5f) {
                    touchControls.setGamepadInUse(true);
                    break;
                }
            }
        }
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    public void onBackPressed() {
        if (menu != null) {
            menu.show();
            return;
        }
        super.onBackPressed();
    }

    // Controls start hidden when a gamepad is connected (a handheld's built-in one included) and go when
    // one connects; a touch brings them back.
    @Override
    public void onInputDeviceAdded(int deviceId) {
        if (touchControls != null && isGamepad(InputDevice.getDevice(deviceId))) {
            touchControls.setGamepadInUse(true);
        }
    }

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        if (touchControls != null && !anyGamepadConnected()) {
            touchControls.setGamepadInUse(false);
        }
    }

    @Override
    public void onInputDeviceChanged(int deviceId) {
    }

    private static boolean isGamepadEvent(int source) {
        return (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                || (source & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD;
    }

    private static boolean isGamepad(InputDevice device) {
        if (device == null || device.isVirtual()) {
            return false;
        }
        int sources = device.getSources();
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    private static boolean anyGamepadConnected() {
        for (int id : InputDevice.getDeviceIds()) {
            if (isGamepad(InputDevice.getDevice(id))) {
                return true;
            }
        }
        return false;
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
