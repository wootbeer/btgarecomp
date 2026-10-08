package io.github.wootbeer.btgarecomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The app's entry point. Starts the game straight away once a ROM is stored; otherwise asks for
 * one with the system document picker first, and copies the chosen file into the app's storage
 * as rom-import.bin. The game validates and stores it at startup (btga::android::
 * import_picked_rom in src/android/android_startup.cpp), so the hash check and byte-order
 * handling stay the same as on desktop.
 *
 * Picking happens here, before the game runs, because the picker covering the game would take
 * its rendering surface away.
 */
public class RomPickerActivity extends Activity {
    private static final String TAG = "BTGA";
    private static final int PICK_ROM = 1;

    // Where librecomp keeps the validated ROM: <app folder>/<game id>.z64 (see main.cpp).
    static final String STORED_ROM = "btga.n64.us.1.0.z64";
    static final String PICKED_ROM = "rom-import.bin";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (savedInstanceState != null) {
            // Recreated while the picker was open; its result still arrives.
            return;
        }
        if (new File(getFilesDir(), STORED_ROM).exists()) {
            startGame();
            return;
        }

        new AlertDialog.Builder(this)
                .setTitle(R.string.app_name)
                .setMessage(R.string.rom_prompt)
                .setPositiveButton(R.string.rom_choose, (dialog, which) -> openPicker())
                .setNegativeButton(android.R.string.cancel, (dialog, which) -> finish())
                .setOnCancelListener(dialog -> finish())
                .show();
    }

    private void openPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, PICK_ROM);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_ROM) {
            return;
        }
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            finish();
            return;
        }

        Uri uri = data.getData();
        new Thread(() -> {
            boolean copied = copyToStorage(uri);
            runOnUiThread(() -> {
                if (copied) {
                    startGame();
                } else {
                    new AlertDialog.Builder(this)
                            .setTitle(R.string.app_name)
                            .setMessage(R.string.rom_copy_failed)
                            .setPositiveButton(android.R.string.ok, (dialog, which) -> finish())
                            .setOnCancelListener(dialog -> finish())
                            .show();
                }
            });
        }).start();
    }

    private boolean copyToStorage(Uri uri) {
        File target = new File(getFilesDir(), PICKED_ROM);
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(target)) {
            if (in == null) {
                return false;
            }
            byte[] buf = new byte[1024 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            return true;
        } catch (IOException e) {
            Log.e(TAG, "Couldn't copy the chosen ROM", e);
            target.delete();
            return false;
        }
    }

    private void startGame() {
        startActivity(new Intent(this, BattleTanxActivity.class));
        finish();
    }
}
