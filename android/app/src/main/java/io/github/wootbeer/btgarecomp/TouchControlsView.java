package io.github.wootbeer.btgarecomp;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.os.Handler;
import android.os.Looper;
import android.view.MotionEvent;
import android.view.View;

/**
 * The on-screen touch controls, laid over the game's surface. The controls themselves (layout, touch
 * handling, the N64 buttons they press) live in native code, src/android/touch_controls.cpp on top of
 * descore's touch module; this view forwards touches and the Touch Options to it and draws the shapes it
 * reports, the same flat translucent gray as descore's GL drawing.
 *
 * Shown only while a match takes input (not on the launcher or under the game's own menus), and hidden
 * while a gamepad is in use: any gamepad input hides them, touching the screen brings them back. A touch
 * that misses every control goes on to the game beneath, for its menus.
 */
public class TouchControlsView extends View {
    private static final String PREFS = "touch";
    private static final String PREF_SCALE_STEP = "scale_step";
    private static final String PREF_OPACITY = "opacity";
    private static final String PREF_DPAD = "dpad";

    /** descore's Size steps (DESCORE_TOUCH_SCALE_*). */
    static final float[] SCALE_VALUES = { 0.90f, 0.95f, 1.00f, 1.10f, 1.20f, 1.35f, 1.50f, 1.65f, 1.80f };
    static final int SCALE_DEFAULT_STEP = 2;
    /** descore's default opacity, which is also the lowest. */
    static final float OPACITY_MIN = 0.30f;

    private static final int FLOATS_PER_SHAPE = 7;
    private static final int POLL_MS = 250;

    /** Called when the MENU control is tapped. */
    interface MenuListener {
        void onMenuRequested();
    }

    private final SharedPreferences prefs;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float[] shapes = new float[FLOATS_PER_SHAPE * 64];
    private final String[] labels = new String[32];
    private MenuListener menuListener;

    private boolean active;
    private boolean gamepadInUse;
    private boolean laidOut;

    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            boolean nowActive = nativeControlsActive();
            if (nowActive != active) {
                active = nowActive;
                invalidate();
            }
            handler.postDelayed(this, POLL_MS);
        }
    };

    public TouchControlsView(Context context) {
        super(context);
        prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        fill.setStyle(Paint.Style.FILL);
        label.setTextAlign(Paint.Align.CENTER);
        label.setFakeBoldText(true);
    }

    void setMenuListener(MenuListener listener) {
        menuListener = listener;
    }

    int getScaleStep() {
        return Math.max(0, Math.min(SCALE_VALUES.length - 1, prefs.getInt(PREF_SCALE_STEP, SCALE_DEFAULT_STEP)));
    }

    float getOpacity() {
        return Math.max(OPACITY_MIN, Math.min(1.0f, prefs.getFloat(PREF_OPACITY, OPACITY_MIN)));
    }

    boolean isDpad() {
        return prefs.getBoolean(PREF_DPAD, false);
    }

    void setScaleStep(int step) {
        prefs.edit().putInt(PREF_SCALE_STEP, step).apply();
        relayout();
    }

    void setOpacity(float opacity) {
        prefs.edit().putFloat(PREF_OPACITY, opacity).apply();
        relayout();
    }

    void setDpad(boolean dpad) {
        prefs.edit().putBoolean(PREF_DPAD, dpad).apply();
        relayout();
    }

    /** A gamepad was used (true), or the screen was touched (false). */
    void setGamepadInUse(boolean inUse) {
        if (inUse == gamepadInUse) {
            return;
        }
        gamepadInUse = inUse;
        nativeSetGamepadConnected(inUse);
        invalidate();
    }

    private void relayout() {
        if (getWidth() <= 0 || getHeight() <= 0) {
            return;
        }
        nativeLayout(getWidth(), getHeight(), getResources().getDisplayMetrics().density,
                getScaleStep(), getOpacity(), isDpad());
        if (!laidOut) {
            laidOut = true;
            nativeSetGamepadConnected(gamepadInUse);
        }
        invalidate();
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        handler.post(poll);
    }

    @Override
    protected void onDetachedFromWindow() {
        handler.removeCallbacks(poll);
        super.onDetachedFromWindow();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        relayout();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (!active || !laidOut) {
            return;
        }
        int count = nativeGetShapes(shapes);
        for (int i = 0; i < count; i++) {
            int o = i * FLOATS_PER_SHAPE;
            int kind = (int) shapes[o];
            int button = (int) shapes[o + 1];
            float x = shapes[o + 2], y = shapes[o + 3], w = shapes[o + 4], h = shapes[o + 5];
            float alpha = shapes[o + 6];
            fill.setARGB(Math.round(alpha * 255), 179, 179, 179);
            if (kind == 1) {
                canvas.drawCircle(x, y, w, fill);
                continue;
            }
            canvas.drawRect(x, y, x + w, y + h, fill);
            if (button >= 0 && button < labels.length) {
                if (labels[button] == null) {
                    labels[button] = nativeButtonLabel(button);
                }
                label.setARGB(Math.round(Math.min(1.0f, alpha * 2.0f) * 255), 255, 255, 255);
                label.setTextSize(Math.min(h * 0.45f, w * 0.3f));
                canvas.drawText(labels[button], x + w * 0.5f,
                        y + h * 0.5f - (label.descent() + label.ascent()) * 0.5f, label);
            }
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            if (!active || !laidOut) {
                return false;
            }
            setGamepadInUse(false);
            if (!nativeHitTest(event.getX(), event.getY())) {
                return false; // to the game beneath
            }
        }

        boolean menu = false;
        if (action == MotionEvent.ACTION_CANCEL) {
            for (int i = 0; i < event.getPointerCount(); i++) {
                nativeTouch(MotionEvent.ACTION_UP, event.getPointerId(i), event.getX(i), event.getY(i));
            }
        } else {
            int first = (action == MotionEvent.ACTION_POINTER_DOWN
                    || action == MotionEvent.ACTION_POINTER_UP) ? event.getActionIndex() : 0;
            int count = (action == MotionEvent.ACTION_MOVE) ? event.getPointerCount() : 1;
            for (int i = first; i < count + first; i++) {
                menu |= (nativeTouch(action, event.getPointerId(i), event.getX(i), event.getY(i)) & 2) != 0;
            }
        }
        invalidate();
        if (menu && menuListener != null) {
            menuListener.onMenuRequested();
        }
        return true;
    }

    private static native void nativeLayout(int width, int height, float density, int scaleStep, float opacity,
            boolean dpad);
    private static native int nativeTouch(int action, int pointerId, float x, float y);
    private static native boolean nativeHitTest(float x, float y);
    private static native void nativeSetGamepadConnected(boolean connected);
    private static native int nativeGetShapes(float[] out);
    private static native String nativeButtonLabel(int button);
    private static native boolean nativeControlsActive();
}
