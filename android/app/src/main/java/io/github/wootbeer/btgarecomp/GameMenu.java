package io.github.wootbeer.btgarecomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.Locale;

/**
 * The app's own menu, opened with Back or the on-screen MENU control (as in gsrandroid's menu): Touch
 * Controls (movement as a joystick or a D-pad, size, opacity), Change ROM and Quit. The game's own
 * settings stay in its in-game menus.
 */
final class GameMenu {
    private final Activity activity;
    private final TouchControlsView touch;
    private AlertDialog dialog;

    GameMenu(Activity activity, TouchControlsView touch) {
        this.activity = activity;
        this.touch = touch;
    }

    void show() {
        if (dialog != null && dialog.isShowing()) {
            return;
        }

        LinearLayout layout = new LinearLayout(activity);
        layout.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(20);
        layout.setPadding(pad, dp(8), pad, dp(8));

        heading(layout, "Touch Controls");

        text(layout, "Movement", Color.LTGRAY, 14);
        RadioGroup movement = new RadioGroup(activity);
        movement.setOrientation(RadioGroup.HORIZONTAL);
        RadioButton joystick = new RadioButton(activity);
        joystick.setText("Joystick");
        joystick.setId(View.generateViewId());
        RadioButton dpad = new RadioButton(activity);
        dpad.setText("D-pad");
        dpad.setId(View.generateViewId());
        movement.addView(joystick);
        movement.addView(dpad);
        movement.check(touch.isDpad() ? dpad.getId() : joystick.getId());
        movement.setOnCheckedChangeListener((group, id) -> touch.setDpad(id == dpad.getId()));
        layout.addView(movement);

        final TextView sizeLabel = text(layout, "", Color.LTGRAY, 14);
        SeekBar size = new SeekBar(activity);
        size.setMax(TouchControlsView.SCALE_VALUES.length - 1);
        size.setProgress(touch.getScaleStep());
        sizeLabel.setText(sizeText(touch.getScaleStep()));
        size.setOnSeekBarChangeListener(new SeekBarListener() {
            @Override
            public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                sizeLabel.setText(sizeText(progress));
                touch.setScaleStep(progress);
            }
        });
        layout.addView(size);

        final int opacityMin = Math.round(TouchControlsView.OPACITY_MIN * 100);
        final TextView opacityLabel = text(layout, "", Color.LTGRAY, 14);
        SeekBar opacity = new SeekBar(activity);
        opacity.setMax(100 - opacityMin);
        int opacityPercent = Math.round(touch.getOpacity() * 100);
        opacity.setProgress(opacityPercent - opacityMin);
        opacityLabel.setText(opacityText(opacityPercent));
        opacity.setOnSeekBarChangeListener(new SeekBarListener() {
            @Override
            public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                int percent = progress + opacityMin;
                opacityLabel.setText(opacityText(percent));
                touch.setOpacity(percent / 100.0f);
            }
        });
        layout.addView(opacity);

        text(layout, "The controls hide while a gamepad is in use; touch the screen to bring them back.",
                Color.GRAY, 12);

        heading(layout, "Game");
        button(layout, "Change ROM", v -> confirmChangeRom());
        button(layout, "Quit", v -> quit());

        ScrollView scroll = new ScrollView(activity);
        scroll.addView(layout);
        dialog = new AlertDialog.Builder(activity)
                .setTitle(R.string.app_name)
                .setView(scroll)
                .setPositiveButton("Close", null)
                .create();
        dialog.show();
    }

    /**
     * Change ROM (as in gsrandroid): after a confirmation, removes only the stored ROM and closes the
     * app, so the next start asks for one. Saves and settings are kept.
     */
    private void confirmChangeRom() {
        new AlertDialog.Builder(activity)
                .setTitle("Change ROM?")
                .setMessage("This removes the stored ROM and closes the game. Your saves and settings are kept.\n\n"
                        + "Open the app again to pick a ROM file.")
                .setPositiveButton("Change ROM", (d, which) -> {
                    File rom = new File(activity.getFilesDir(), RomPickerActivity.STORED_ROM);
                    if (rom.exists() && !rom.delete()) {
                        Toast.makeText(activity, "Could not remove the ROM", Toast.LENGTH_LONG).show();
                        return;
                    }
                    Toast.makeText(activity, "ROM removed. Open the app again to pick one.", Toast.LENGTH_LONG).show();
                    quit();
                })
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    /** The native game has no clean way to stop part-way, so the process ends with the activity. */
    private void quit() {
        if (dialog != null) {
            dialog.dismiss();
        }
        activity.finishAffinity();
        activity.getWindow().getDecorView().postDelayed(() -> System.exit(0), 500);
    }

    private static String sizeText(int step) {
        return String.format(Locale.US, "Size: %.2fx", TouchControlsView.SCALE_VALUES[step]);
    }

    private static String opacityText(int percent) {
        return String.format(Locale.US, "Opacity: %d%%", percent);
    }

    private void heading(LinearLayout parent, String title) {
        TextView tv = text(parent, title, Color.WHITE, 16);
        tv.setPadding(0, dp(14), 0, dp(4));
        tv.setTypeface(tv.getTypeface(), android.graphics.Typeface.BOLD);
    }

    private TextView text(LinearLayout parent, String text, int color, float sizeSp) {
        TextView tv = new TextView(activity);
        tv.setText(text);
        tv.setTextColor(color);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp);
        tv.setPadding(0, dp(6), 0, 0);
        parent.addView(tv);
        return tv;
    }

    private void button(LinearLayout parent, String text, View.OnClickListener listener) {
        Button b = new Button(activity);
        b.setText(text);
        b.setOnClickListener(listener);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.gravity = Gravity.CENTER_HORIZONTAL;
        parent.addView(b, params);
    }

    private int dp(int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }

    private abstract static class SeekBarListener implements SeekBar.OnSeekBarChangeListener {
        @Override
        public void onStartTrackingTouch(SeekBar bar) {
        }

        @Override
        public void onStopTrackingTouch(SeekBar bar) {
        }
    }
}
