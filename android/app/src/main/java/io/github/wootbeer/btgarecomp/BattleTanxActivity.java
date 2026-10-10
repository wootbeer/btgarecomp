package io.github.wootbeer.btgarecomp;

import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.hardware.input.InputManager;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

/**
 * Hosts the native game (libmain.so, built from the repo-root CMakeLists.txt) through SDL.
 */
public class BattleTanxActivity extends SDLActivity implements InputManager.InputDeviceListener {
    private static final String TAG = "BTGA";

    private static final int PICK_FILES = 1;
    /** Where files chosen for the game (Change ROM, Install Mods) are copied. */
    private static final String PICKED_FOLDER = "picked";

    private TouchControlsView touchControls;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        installAssets();
        super.onCreate(savedInstanceState);

        // SDL's layout holds the game's surface; the touch controls go over it.
        if (mLayout != null && !mBrokenLibraries) {
            touchControls = new TouchControlsView(this);
            mLayout.addView(touchControls, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            touchControls.setMenuListener(this::openGameMenu);

            InputManager inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
            if (inputManager != null) {
                inputManager.registerInputDeviceListener(this, null);
            }
            touchControls.setGamepadInUse(anyGamepadConnected());
        }
    }

    // SDL hides the system bars once, when the game's window goes fullscreen (mFullscreenModeActive).
    // Hide them again whenever the window gets focus back, and on the overlay too: the window takes
    // the system UI flags of all its views together.
    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    // SDL hides the bars again 2 s after they show; do it straight away.
    @Override
    public void onSystemUiVisibilityChange(int visibility) {
        super.onSystemUiVisibilityChange(visibility);
        if ((visibility & View.SYSTEM_UI_FLAG_HIDE_NAVIGATION) == 0
                || (visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
            hideSystemBars();
        }
    }

    private void hideSystemBars() {
        if (!mFullscreenModeActive) {
            return;
        }
        int flags = View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE;
        getWindow().getDecorView().setSystemUiVisibility(flags);
        if (touchControls != null) {
            touchControls.setSystemUiVisibility(flags);
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

    /** The game's own menu opens (and closes) on Escape; the MENU control and Back send it. */
    private void openGameMenu() {
        onNativeKeyDown(KeyEvent.KEYCODE_ESCAPE);
        onNativeKeyUp(KeyEvent.KEYCODE_ESCAPE);
    }

    // Back opens and closes the game's menu once the game is running; on the launcher it still closes
    // the app.
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (touchControls != null && event.getKeyCode() == KeyEvent.KEYCODE_BACK && touchControls.isGameStarted()) {
            if (event.getAction() == KeyEvent.ACTION_UP) {
                openGameMenu();
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

    /**
     * Called by the game (src/android/nfd_android.cpp, its file dialogs) to have the user choose a file,
     * or several: shows the system document picker, copies the choices into the app's storage under
     * their own names (Install Mods goes by them) and hands the copies' paths back through
     * nativeFilesPicked, or null if nothing was chosen.
     */
    public void pickFilesForNative(boolean multiple) {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*"); // ROM dumps and mods have no registered MIME type
            intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, multiple);
            try {
                startActivityForResult(intent, PICK_FILES);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "No document picker", e);
                nativeFilesPicked(null);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != PICK_FILES) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        List<Uri> uris = new ArrayList<>();
        if (resultCode == RESULT_OK && data != null) {
            ClipData clips = data.getClipData();
            if (clips != null) {
                for (int i = 0; i < clips.getItemCount(); i++) {
                    uris.add(clips.getItemAt(i).getUri());
                }
            } else if (data.getData() != null) {
                uris.add(data.getData());
            }
        }
        if (uris.isEmpty()) {
            nativeFilesPicked(null);
            return;
        }
        new Thread(() -> {
            // A fresh folder each time; the game has finished with the last pick's copies by now.
            File folder = new File(getFilesDir(), PICKED_FOLDER);
            deleteRecursively(folder);
            folder.mkdirs();
            List<String> paths = new ArrayList<>();
            for (int i = 0; i < uris.size(); i++) {
                File target = new File(folder, displayName(uris.get(i), "file" + i));
                if (RomPickerActivity.copyToFile(this, uris.get(i), target)) {
                    paths.add(target.getAbsolutePath());
                }
            }
            nativeFilesPicked(paths.isEmpty() ? null : paths.toArray(new String[0]));
        }).start();
    }

    /** The document's file name, made safe as one; `fallback` if it has none. */
    private String displayName(Uri uri, String fallback) {
        String name = null;
        try (Cursor cursor = getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME },
                null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                name = cursor.getString(0);
            }
        } catch (Exception e) {
            Log.w(TAG, "No name for " + uri, e);
        }
        if (name == null || name.isEmpty()) {
            return fallback;
        }
        return name.replace('/', '_');
    }

    private static native void nativeFilesPicked(String[] paths);

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
