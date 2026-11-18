package org.ps2native.recomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.util.SparseArray;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

// The on-screen DualShock for phones without a controller: a left and a right stick, the four face buttons, L1/L2/R1/R2 and
// Start/Select. It covers the screen, consumes every touch (so the game only sees the pad) and sends the state to libmain with
// MainActivity.setVirtualPad; window.cpp puts it into the first controller port. MainActivity hides this view when Android
// reports a gamepad (a PS3/PS4/PS5 controller over Bluetooth or USB), so a real controller and the screen never fight.
public class VirtualPadView extends View {
    // The masks window.cpp uses for the pad's buttons.
    private static final int BTN_SELECT = 1;
    private static final int BTN_L3 = 1 << 1;
    private static final int BTN_R3 = 1 << 2;
    private static final int BTN_START = 1 << 3;
    private static final int BTN_UP = 1 << 4;
    private static final int BTN_RIGHT = 1 << 5;
    private static final int BTN_DOWN = 1 << 6;
    private static final int BTN_LEFT = 1 << 7;
    private static final int BTN_L2 = 1 << 8;
    private static final int BTN_R2 = 1 << 9;
    private static final int BTN_L1 = 1 << 10;
    private static final int BTN_R1 = 1 << 11;
    private static final int BTN_TRIANGLE = 1 << 12;
    private static final int BTN_CIRCLE = 1 << 13;
    private static final int BTN_CROSS = 1 << 14;
    private static final int BTN_SQUARE = 1 << 15;
    private static final int DPAD = BTN_UP | BTN_RIGHT | BTN_DOWN | BTN_LEFT;

    private static final int CTRL_NONE = 0;
    private static final int CTRL_LEFT_STICK = 1;
    private static final int CTRL_RIGHT_STICK = 2;
    private static final int CTRL_BUTTON = 3;
    private static final int CTRL_MENU = 4;

    private static final class Pointer {
        int control = CTRL_NONE;
        int button = 0;
    }

    private final SparseArray<Pointer> pointers = new SparseArray<>();
    private int buttons = 0;
    private float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;

    private float width = 0.0f, height = 0.0f, unit = 0.0f;
    private float leftCx, leftCy, leftR;
    private float rightCx, rightCy;
    private float rightStickCx, rightStickCy, rightR;
    private float faceR, faceOffset;
    private final float[] faceCx = new float[4], faceCy = new float[4];
    private final int[] faceMasks = {BTN_TRIANGLE, BTN_CIRCLE, BTN_CROSS, BTN_SQUARE};
    private final String[] faceText = {"\u25B3", "\u25CB", "\u00D7", "\u25A1"};
    private final int[] faceColors = {0x5533C46A, 0x55E74C3C, 0x553498DB, 0x55E91E63};
    private final RectF[] rects = new RectF[6];
    private final int[] rectMasks = {BTN_L1, BTN_L2, BTN_R1, BTN_R2, BTN_SELECT, BTN_START};
    private final String[] rectText = {"L1", "L2", "R1", "R2", "SELECT", "START"};
    private final RectF menuRect = new RectF();

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable heartbeat = new Runnable() {
        @Override
        public void run() {
            sendState();  // also gives libmain the middle if it is loaded after the view was made
            handler.postDelayed(this, 250);
        }
    };

    public VirtualPadView(Context context) {
        super(context);
        setFocusable(false);
        setClickable(true);
        for (int i = 0; i < rects.length; ++i) rects[i] = new RectF();
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setColor(0x88FFFFFF);
        label.setColor(0xEEFFFFFF);
        label.setTextAlign(Paint.Align.CENTER);
        label.setTypeface(Typeface.DEFAULT_BOLD);
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        handler.post(heartbeat);
    }

    @Override
    protected void onDetachedFromWindow() {
        handler.removeCallbacks(heartbeat);
        releaseAll();
        super.onDetachedFromWindow();
    }

    @Override
    protected void onVisibilityChanged(View changedView, int visibility) {
        super.onVisibilityChanged(changedView, visibility);
        if (visibility != VISIBLE) releaseAll();
    }

    private void layout() {
        final float w = getWidth(), h = getHeight();
        if (w == width && h == height) return;
        width = w;
        height = h;
        unit = Math.min(w, h);
        final float margin = unit * 0.035f;

        leftR = unit * 0.115f;
        leftCx = margin + leftR * 1.25f;
        leftCy = h - margin - leftR * 1.15f;
        rightR = unit * 0.100f;
        rightStickCx = w - margin - unit * 0.455f;
        rightStickCy = h - margin - rightR * 1.15f;
        rightCx = w - margin - unit * 0.155f;
        rightCy = leftCy;

        faceR = unit * 0.050f;
        faceOffset = unit * 0.112f;
        faceCx[0] = rightCx;                 faceCy[0] = rightCy - faceOffset;  // triangle
        faceCx[1] = rightCx + faceOffset;    faceCy[1] = rightCy;               // circle
        faceCx[2] = rightCx;                 faceCy[2] = rightCy + faceOffset;  // cross
        faceCx[3] = rightCx - faceOffset;    faceCy[3] = rightCy;               // square

        final float sw = unit * 0.105f, sh = unit * 0.048f, gap = unit * 0.010f;
        rects[0].set(margin, margin, margin + sw, margin + sh);                       // L1
        rects[1].set(margin, margin + sh + gap, margin + sw, margin + sh * 2 + gap);  // L2
        rects[2].set(w - margin - sw, margin, w - margin, margin + sh);                       // R1
        rects[3].set(w - margin - sw, margin + sh + gap, w - margin, margin + sh * 2 + gap);  // R2
        final float bw = unit * 0.115f, bh = unit * 0.044f;
        rects[4].set(w * 0.5f - bw - unit * 0.015f, margin, w * 0.5f - unit * 0.015f, margin + bh);  // select
        rects[5].set(w * 0.5f + unit * 0.015f, margin, w * 0.5f + unit * 0.015f + bw, margin + bh);  // start
        menuRect.set(w * 0.5f + unit * 0.22f, margin, w * 0.5f + unit * 0.33f, margin + bh);        // quick menu (F1)

        label.setTextSize(unit * 0.042f);
        stroke.setStrokeWidth(Math.max(2.0f, unit * 0.004f));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        layout();
        if (width <= 0 || height <= 0) return;

        // the two sticks: base, then the knob where the finger is
        drawStick(canvas, leftCx, leftCy, leftR, lx, ly);
        drawStick(canvas, rightStickCx, rightStickCy, rightR, rx, ry);

        // the four face buttons
        for (int i = 0; i < 4; ++i) {
            final boolean down = (buttons & faceMasks[i]) != 0;
            fill.setColor(down ? (faceColors[i] | 0x66000000) : faceColors[i]);
            canvas.drawCircle(faceCx[i], faceCy[i], faceR, fill);
            stroke.setColor(down ? 0xFFFFFFFF : 0x88FFFFFF);
            canvas.drawCircle(faceCx[i], faceCy[i], faceR, stroke);
            label.setTextSize(unit * 0.050f);
            label.setColor(0xFFFFFFFF);
            canvas.drawText(faceText[i], faceCx[i], faceCy[i] + unit * 0.017f, label);
        }

        // shoulders and Start/Select
        label.setTextSize(unit * 0.032f);
        for (int i = 0; i < rects.length; ++i) {
            final boolean down = (buttons & rectMasks[i]) != 0;
            fill.setColor(down ? 0x66FFFFFF : 0x22FFFFFF);
            canvas.drawRoundRect(rects[i], unit * 0.012f, unit * 0.012f, fill);
            stroke.setColor(down ? 0xFFFFFFFF : 0x88FFFFFF);
            canvas.drawRoundRect(rects[i], unit * 0.012f, unit * 0.012f, stroke);
            label.setColor(0xEEFFFFFF);
            final float cx = (rects[i].left + rects[i].right) * 0.5f;
            final float cy = (rects[i].top + rects[i].bottom) * 0.5f + unit * 0.011f;
            canvas.drawText(rectText[i], cx, cy, label);
        }

        // the quick menu (F1 on the keyboard), useful to change resolution, picture look or speed from the phone
        fill.setColor(0x22FFFFFF);
        canvas.drawRoundRect(menuRect, unit * 0.012f, unit * 0.012f, fill);
        stroke.setColor(0x88FFFFFF);
        canvas.drawRoundRect(menuRect, unit * 0.012f, unit * 0.012f, stroke);
        label.setColor(0xEEFFFFFF);
        label.setTextSize(unit * 0.028f);
        canvas.drawText("MENU", (menuRect.left + menuRect.right) * 0.5f, (menuRect.top + menuRect.bottom) * 0.5f + unit * 0.010f, label);
    }

    private void drawStick(Canvas canvas, float cx, float cy, float radius, float x, float y) {
        fill.setColor(0x18FFFFFF);
        canvas.drawCircle(cx, cy, radius, fill);
        stroke.setColor(0x66FFFFFF);
        canvas.drawCircle(cx, cy, radius, stroke);
        final float kx = cx + x * radius * 0.72f, ky = cy + y * radius * 0.72f;
        fill.setColor((x != 0.0f || y != 0.0f) ? 0x66FFFFFF : 0x33FFFFFF);
        canvas.drawCircle(kx, ky, radius * 0.40f, fill);
        stroke.setColor(0xAAFFFFFF);
        canvas.drawCircle(kx, ky, radius * 0.40f, stroke);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        layout();
        final int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            final int index = event.getActionIndex();
            final Pointer p = hitTest(event.getX(index), event.getY(index));
            if (p != null) {
                pointers.put(event.getPointerId(index), p);
                if (p.control == CTRL_BUTTON) {
                    buttons |= p.button;
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                } else if (p.control == CTRL_MENU) {
                    MainActivity.toggleMenu();
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                }
                sendState();
                invalidate();
            }
        } else if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < event.getPointerCount(); ++i) {
                final Pointer p = pointers.get(event.getPointerId(i));
                if (p == null) continue;
                if (p.control == CTRL_LEFT_STICK || p.control == CTRL_RIGHT_STICK)
                    moveStick(p.control, event.getX(i), event.getY(i));
            }
            sendState();
            invalidate();
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            final int index = event.getActionIndex();
            final Pointer p = pointers.get(event.getPointerId(index));
            if (p != null) {
                release(p);
                pointers.remove(event.getPointerId(index));
                sendState();
                invalidate();
            }
        } else if (action == MotionEvent.ACTION_CANCEL) {
            releaseAll();
        }
        return true;  // the game must not see the touches used by the pad
    }

    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }

    private Pointer hitTest(float x, float y) {
        for (int i = 0; i < 4; ++i) {
            final float dx = x - faceCx[i], dy = y - faceCy[i];
            if (dx * dx + dy * dy <= faceR * faceR * 1.8f) {
                final Pointer p = new Pointer();
                p.control = CTRL_BUTTON;
                p.button = faceMasks[i];
                return p;
            }
        }
        for (int i = 0; i < rects.length; ++i) {
            if (rects[i].contains(x, y)) {
                final Pointer p = new Pointer();
                p.control = CTRL_BUTTON;
                p.button = rectMasks[i];
                return p;
            }
        }
        if (menuRect.contains(x, y)) {
            final Pointer p = new Pointer();
            p.control = CTRL_MENU;
            return p;
        }
        final float dlx = x - leftCx, dly = y - leftCy;
        if (dlx * dlx + dly * dly <= leftR * leftR * 2.0f) {
            final Pointer p = new Pointer();
            p.control = CTRL_LEFT_STICK;
            return p;
        }
        final float drx = x - rightStickCx, dry = y - rightStickCy;
        if (drx * drx + dry * dry <= rightR * rightR * 2.0f) {
            final Pointer p = new Pointer();
            p.control = CTRL_RIGHT_STICK;
            return p;
        }
        return null;
    }

    private void moveStick(int control, float x, float y) {
        final boolean left = control == CTRL_LEFT_STICK;
        final float cx = left ? leftCx : rightStickCx;
        final float cy = left ? leftCy : rightStickCy;
        final float radius = left ? leftR : rightR;
        float dx = (x - cx) / radius, dy = (y - cy) / radius;
        float magnitude = (float) Math.hypot(dx, dy);
        if (magnitude > 1.0f) {
            dx /= magnitude;
            dy /= magnitude;
            magnitude = 1.0f;
        }
        if (magnitude < 0.12f) {
            dx = dy = 0.0f;
        } else {
            final float scaled = (magnitude - 0.12f) / 0.88f;
            dx = dx / magnitude * scaled;
            dy = dy / magnitude * scaled;
        }
        if (left) {
            lx = dx;
            ly = dy;
            buttons &= ~DPAD;
            if (lx > 0.55f) buttons |= BTN_RIGHT;
            if (lx < -0.55f) buttons |= BTN_LEFT;
            if (ly > 0.55f) buttons |= BTN_DOWN;
            if (ly < -0.55f) buttons |= BTN_UP;
        } else {
            rx = dx;
            ry = dy;
        }
    }

    private void release(Pointer p) {
        if (p.control == CTRL_BUTTON) {
            buttons &= ~p.button;
        } else if (p.control == CTRL_LEFT_STICK) {
            lx = ly = 0.0f;
            buttons &= ~DPAD;
        } else if (p.control == CTRL_RIGHT_STICK) {
            rx = ry = 0.0f;
        }
    }

    void releaseAll() {
        pointers.clear();
        buttons = 0;
        lx = ly = rx = ry = 0.0f;
        sendState();
        invalidate();
    }

    private void sendState() {
        MainActivity.setVirtualPad(buttons, lx, ly, rx, ry);
    }
}
