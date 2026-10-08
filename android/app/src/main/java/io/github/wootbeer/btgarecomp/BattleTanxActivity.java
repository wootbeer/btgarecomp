package io.github.wootbeer.btgarecomp;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;

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

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        installAssets();
        super.onCreate(savedInstanceState);
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
