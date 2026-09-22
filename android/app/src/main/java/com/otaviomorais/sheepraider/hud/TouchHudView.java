package com.otaviomorais.sheepraider.hud;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.Log;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Fullscreen transparent overlay for on-screen touch controls.
 * Designed with authentic PlayStation layout, dedicated settings button,
 * and zero accidental edit-mode triggers during intense gameplay.
 */
public class TouchHudView extends View {

    private static final String TAG = "rechan-hud";
    private static final boolean DEBUG = Log.isLoggable(TAG, Log.DEBUG);

    /** One on-screen control (button or joystick or settings menu). */
    public static class Control {
        final String id;
        final String label;
        final int buttonId; // -1 for joystick, -2 for settings/edit button
        float nx, ny;       // normalized center within safe rect [0..1]
        float nr;           // normalized radius relative to min(safeW, safeH)
        boolean interactive = true;

        Control(String id, String label, int buttonId, float nx, float ny, float nr) {
            this.id = id;
            this.label = label;
            this.buttonId = buttonId;
            this.nx = nx;
            this.ny = ny;
            this.nr = nr;
        }
    }

    private final HudController controller;
    private final List<Control> controls = new ArrayList<>();
    private Control joystick;
    private Control editButton;

    private int insetL, insetT, insetR, insetB;
    private float opacity = 0.50f;
    private boolean lowContrast = false;
    private boolean editMode = false;
    private Control selectedControl;
    private boolean visible = false;
    private int context = HudBridge.CONTEXT_HIDDEN;

    // Pointer tracking: pointerId -> control id ("" for unassigned)
    private final android.util.SparseArray<String> pointerControls = new android.util.SparseArray<>();
    private final Set<Integer> pressedButtons = new HashSet<>();
    private int joyPointerId = -1;
    private float joyKnobDx, joyKnobDy;

    // Edit-mode chips
    private final List<RectF> chipBounds = new ArrayList<>();
    private static final String[] CHIPS = {"Menor", "Maior", "Opac-", "Opac+", "Contorno", "Concluir"};

    // Paints
    private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint strokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint knobPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    public TouchHudView(Context context, HudController controller) {
        super(context);
        this.controller = controller;
        setFocusable(false);
        setClickable(true);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);

        // Analog stick (left)
        joystick = new Control("joy", "", -1, 0.160f, 0.720f, 0.130f);
        controls.add(joystick);

        // Action Buttons: Classic PlayStation diamond layout
        controls.add(new Control("cross", "✕", HudBridge.BTN_CROSS, 0.860f, 0.820f, 0.072f));      // Bottom
        controls.add(new Control("circle", "○", HudBridge.BTN_CIRCLE, 0.940f, 0.700f, 0.072f));    // Right
        controls.add(new Control("square", "□", HudBridge.BTN_SQUARE, 0.780f, 0.700f, 0.072f));    // Left
        controls.add(new Control("triangle", "△", HudBridge.BTN_TRIANGLE, 0.860f, 0.580f, 0.072f));// Top

        // Shoulder Buttons (L1, L2, R1, R2)
        controls.add(new Control("L1", "L1", HudBridge.BTN_L1, 0.080f, 0.400f, 0.055f));
        controls.add(new Control("L2", "L2", HudBridge.BTN_L2, 0.080f, 0.260f, 0.055f));
        controls.add(new Control("R1", "R1", HudBridge.BTN_R1, 0.920f, 0.400f, 0.055f));
        controls.add(new Control("R2", "R2", HudBridge.BTN_R2, 0.920f, 0.260f, 0.055f));

        // Center / System Buttons
        controls.add(new Control("select", "SEL", HudBridge.BTN_SELECT, 0.400f, 0.940f, 0.040f));
        controls.add(new Control("start", "START", HudBridge.BTN_START, 0.600f, 0.940f, 0.040f));

        // Settings / Edit button (top center - tap only, NEVER accidentally triggered)
        editButton = new Control("edit", "⚙", -2, 0.500f, 0.060f, 0.040f);
        controls.add(editButton);

        JSONObject saved = controller.loadLayout();
        if (saved != null) {
            applyLayout(saved);
        }

        fillPaint.setColor(Color.WHITE);
        strokePaint.setColor(Color.WHITE);
        strokePaint.setStyle(Paint.Style.STROKE);
        knobPaint.setColor(Color.WHITE);
        textPaint.setColor(Color.WHITE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setFakeBoldText(true);
    }

    void setSafeInsets(int l, int t, int r, int b) {
        insetL = l;
        insetT = t;
        insetR = r;
        insetB = b;
        invalidate();
    }

    void animateVisibility(boolean show, int hudContext) {
        this.context = hudContext;
        applyContextToControls();
        if (show == visible) {
            invalidate();
            return;
        }
        visible = show;
        if (show) {
            setVisibility(VISIBLE);
            setAlpha(0f);
            setScaleX(0.96f);
            setScaleY(0.96f);
            animate().alpha(1f).scaleX(1f).scaleY(1f).setDuration(180).start();
        } else {
            releaseAll();
            animate().alpha(0f).setDuration(180)
                    .withEndAction(() -> {
                        if (!visible) {
                            setVisibility(INVISIBLE);
                            exitEditMode();
                        }
                    }).start();
        }
        log("visibility -> " + show + " context=" + hudContext);
    }

    void releaseAll() {
        for (Control c : controls) {
            if (c.buttonId >= 0 && pressedButtons.contains(c.buttonId)) {
                HudBridge.nativePostButton(c.buttonId, false);
            }
        }
        pressedButtons.clear();
        if (joyPointerId != -1) {
            HudBridge.nativePostAxis(HudBridge.AXIS_LEFT_X, 0f);
            HudBridge.nativePostAxis(HudBridge.AXIS_LEFT_Y, 0f);
            joyPointerId = -1;
        }
        joyKnobDx = joyKnobDy = 0f;
        pointerControls.clear();
        invalidate();
    }

    private void applyContextToControls() {
        final boolean active = context != HudBridge.CONTEXT_HIDDEN;
        for (Control c : controls) {
            c.interactive = active;
        }
    }

    private JSONObject toJSON() {
        try {
            JSONObject root = new JSONObject();
            JSONObject ctrls = new JSONObject();
            for (Control c : controls) {
                ctrls.put(c.id, new JSONObject()
                        .put("x", c.nx).put("y", c.ny).put("r", c.nr));
            }
            root.put("controls", ctrls);
            root.put("opacity", opacity);
            root.put("lowContrast", lowContrast);
            return root;
        } catch (JSONException e) {
            return null;
        }
    }

    private void applyLayout(JSONObject root) {
        try {
            JSONObject ctrls = root.optJSONObject("controls");
            if (ctrls != null) {
                for (Control c : controls) {
                    JSONObject pos = ctrls.optJSONObject(c.id);
                    if (pos != null) {
                        c.nx = clamp01((float) pos.optDouble("x", c.nx));
                        c.ny = clamp01((float) pos.optDouble("y", c.ny));
                        c.nr = clamp01((float) pos.optDouble("r", c.nr));
                    }
                }
            }
            opacity = clamp01((float) root.optDouble("opacity", opacity));
            lowContrast = root.optBoolean("lowContrast", false);
        } catch (Exception e) {
            log("applyLayout failed: " + e.getMessage());
        }
    }

    private static float clamp01(float v) {
        return Math.max(0f, Math.min(1f, v));
    }

    private float safeW() {
        return Math.max(1, getWidth() - insetL - insetR);
    }

    private float safeH() {
        return Math.max(1, getHeight() - insetT - insetB);
    }

    private float cx(Control c) {
        return insetL + c.nx * safeW();
    }

    private float cy(Control c) {
        return insetT + c.ny * safeH();
    }

    private float radius(Control c) {
        return c.nr * Math.min(safeW(), safeH());
    }

    private Control controlAt(float x, float y) {
        Control hit = null;
        float bestDist = Float.MAX_VALUE;
        for (Control c : controls) {
            if (!c.interactive) continue;
            float dx = x - cx(c);
            float dy = y - cy(c);
            float dist = (float) Math.hypot(dx, dy);
            float reach = radius(c) * (c == joystick ? 1.8f : 1.35f);
            if (dist <= reach && dist < bestDist) {
                bestDist = dist;
                hit = c;
            }
        }
        return hit;
    }

    // --- Touch handling -----------------------------------------------------------

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!visible) return false;
        if (editMode) {
            return handleEditTouch(event);
        }

        final int action = event.getActionMasked();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int idx = event.getActionIndex();
                int pid = event.getPointerId(idx);
                float x = event.getX(idx);
                float y = event.getY(idx);

                Control c = controlAt(x, y);

                // Check if user tapped the dedicated edit button
                if (c == editButton) {
                    enterEditMode();
                    return true;
                }

                // If no specific button was hit, check if touch is in the left joystick zone
                if (c == null && joyPointerId == -1) {
                    if (x < safeW() * 0.45f && y > safeH() * 0.30f) {
                        c = joystick;
                    }
                }

                pointerControls.put(pid, c != null ? c.id : "");
                if (c != null) {
                    if (c == joystick) {
                        joyPointerId = pid;
                        updateJoystick(x, y);
                    } else if (c.buttonId >= 0) {
                        pressButton(c, true);
                    }
                }
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < event.getPointerCount(); i++) {
                    int pid = event.getPointerId(i);
                    String assigned = pointerControls.get(pid, "");
                    if (assigned.isEmpty()) continue;
                    Control c = findControl(assigned);
                    if (c == null) continue;
                    float x = event.getX(i);
                    float y = event.getY(i);
                    if (c == joystick && pid == joyPointerId) {
                        updateJoystick(x, y);
                    }
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int idx = event.getActionIndex();
                int pid = event.getPointerId(idx);
                releasePointer(pid);
                if (pid == joyPointerId) {
                    joyPointerId = -1;
                    joyKnobDx = joyKnobDy = 0f;
                    HudBridge.nativePostAxis(HudBridge.AXIS_LEFT_X, 0f);
                    HudBridge.nativePostAxis(HudBridge.AXIS_LEFT_Y, 0f);
                    invalidate();
                }
                return true;
            }
            case MotionEvent.ACTION_CANCEL: {
                releaseAll();
                return true;
            }
            default:
                return true;
        }
    }

    private void updateJoystick(float x, float y) {
        float dx = x - cx(joystick);
        float dy = y - cy(joystick);
        float r = radius(joystick);
        float mag = (float) Math.hypot(dx, dy);
        if (mag > r) {
            dx = dx / mag * r;
            dy = dy / mag * r;
        }
        joyKnobDx = dx;
        joyKnobDy = dy;
        // Screen Y is down-positive
        HudBridge.nativePostAxis(HudBridge.AXIS_LEFT_X, clampAxis(dx / r));
        HudBridge.nativePostAxis(HudBridge.AXIS_LEFT_Y, clampAxis(dy / r));
        invalidate();
    }

    private static float clampAxis(float v) {
        return Math.max(-1f, Math.min(1f, v));
    }

    private void pressButton(Control c, boolean down) {
        if (c.buttonId < 0) return;
        if (down) {
            pressedButtons.add(c.buttonId);
            HudBridge.nativePostButton(c.buttonId, true);
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        } else {
            pressedButtons.remove(c.buttonId);
            HudBridge.nativePostButton(c.buttonId, false);
        }
        invalidate();
    }

    private void releasePointer(int pid) {
        String assigned = pointerControls.get(pid, "");
        pointerControls.delete(pid);
        if (assigned.isEmpty()) return;
        Control c = findControl(assigned);
        if (c != null && c != joystick && c.buttonId >= 0) {
            pressButton(c, false);
        }
    }

    private Control findControl(String id) {
        for (Control c : controls) {
            if (c.id.equals(id)) return c;
        }
        return null;
    }

    // --- Edit mode (Dedicated Menu, never triggered accidentally) ----------------

    private void enterEditMode() {
        if (!visible || editMode) return;
        releaseAll();
        editMode = true;
        selectedControl = controls.get(1); // Default select Cross button
        performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
        log("edit mode entered");
        invalidate();
    }

    private void exitEditMode() {
        if (!editMode) return;
        editMode = false;
        JSONObject json = toJSON();
        if (json != null) {
            controller.saveLayout(json);
        }
        invalidate();
    }

    private boolean handleEditTouch(MotionEvent event) {
        final int action = event.getActionMasked();
        switch (action) {
            case MotionEvent.ACTION_DOWN: {
                float x = event.getX();
                float y = event.getY();
                for (int i = 0; i < chipBounds.size(); i++) {
                    if (chipBounds.get(i).contains(x, y)) {
                        onChip(i);
                        return true;
                    }
                }
                Control c = controlAt(x, y);
                if (c != null && c != editButton) {
                    selectedControl = c;
                    pointerControls.put(event.getPointerId(0), c.id);
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                }
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                if (selectedControl != null) {
                    float x = event.getX();
                    float y = event.getY();
                    selectedControl.nx = clamp01((x - insetL) / safeW());
                    selectedControl.ny = clamp01((y - insetT) / safeH());
                    invalidate();
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                pointerControls.clear();
                return true;
            default:
                return true;
        }
    }

    private void onChip(int index) {
        switch (CHIPS[index]) {
            case "Menor":
                if (selectedControl != null) {
                    selectedControl.nr = Math.max(0.035f, selectedControl.nr - 0.008f);
                }
                break;
            case "Maior":
                if (selectedControl != null) {
                    selectedControl.nr = Math.min(0.20f, selectedControl.nr + 0.008f);
                }
                break;
            case "Opac-":
                opacity = clamp01(opacity - 0.05f);
                break;
            case "Opac+":
                opacity = clamp01(opacity + 0.05f);
                break;
            case "Contorno":
                lowContrast = !lowContrast;
                break;
            case "Concluir":
                exitEditMode();
                return;
        }
        invalidate();
    }

    // --- Drawing ---------------------------------------------------------------------

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!visible || getAlpha() <= 0.01f) return;

        int alpha = (int) (opacity * 255);
        float base = Math.min(safeW(), safeH());

        for (Control c : controls) {
            if (!c.interactive && !editMode) continue;
            float x = cx(c);
            float y = cy(c);
            float r = radius(c);
            boolean pressed = c.buttonId >= 0 && pressedButtons.contains(c.buttonId);

            if (lowContrast) {
                strokePaint.setStrokeWidth(Math.max(2f, base * 0.006f));
                strokePaint.setAlpha(c == editButton ? 120 : alpha);
                canvas.drawCircle(x, y, r, strokePaint);
            } else {
                fillPaint.setAlpha(pressed ? Math.min(255, alpha + 90) : (c == editButton ? 100 : alpha));
                canvas.drawCircle(x, y, r, fillPaint);
                strokePaint.setStrokeWidth(Math.max(2f, base * 0.004f));
                strokePaint.setAlpha(c == editButton ? 140 : 200);
                canvas.drawCircle(x, y, r, strokePaint);
            }

            if (c == joystick) {
                float knobR = r * 0.45f;
                knobPaint.setAlpha(Math.min(255, alpha + 60));
                canvas.drawCircle(x + joyKnobDx, y + joyKnobDy, knobR, knobPaint);
            } else if (!c.label.isEmpty()) {
                textPaint.setTextSize(c.label.length() > 2 ? r * 0.45f : r * 0.85f);
                textPaint.setAlpha(lowContrast ? alpha : 255);
                Paint.FontMetrics fm = textPaint.getFontMetrics();
                float textY = y - (fm.ascent + fm.descent) / 2f;
                canvas.drawText(c.label, x, textY, textPaint);
            }

            if (editMode && c == selectedControl) {
                strokePaint.setStrokeWidth(Math.max(3f, base * 0.008f));
                strokePaint.setAlpha(255);
                strokePaint.setColor(0xFFFFC857);
                canvas.drawCircle(x, y, r * 1.15f, strokePaint);
                strokePaint.setColor(Color.WHITE);
            }
        }

        if (editMode) {
            drawChips(canvas);
        }
    }

    private void drawChips(Canvas canvas) {
        chipBounds.clear();
        float base = Math.min(safeW(), safeH());
        float chipH = base * 0.07f;
        float textSize = chipH * 0.42f;
        textPaint.setTextSize(textSize);
        Paint.FontMetrics fm = textPaint.getFontMetrics();
        float x = insetL + safeW() * 0.5f;
        float totalW = 0f;
        float[] widths = new float[CHIPS.length];
        for (int i = 0; i < CHIPS.length; i++) {
            widths[i] = textPaint.measureText(CHIPS[i]) + chipH;
            totalW += widths[i];
        }
        float cx = x - totalW / 2f;
        float cy = insetT + base * 0.03f;
        for (int i = 0; i < CHIPS.length; i++) {
            RectF rect = new RectF(cx, cy, cx + widths[i], cy + chipH);
            chipBounds.add(rect);
            fillPaint.setAlpha(160);
            canvas.drawRoundRect(rect, chipH / 2f, chipH / 2f, fillPaint);
            textPaint.setAlpha(255);
            canvas.drawText(CHIPS[i], rect.centerX(),
                    cy + chipH / 2f - (fm.ascent + fm.descent) / 2f, textPaint);
            cx += widths[i] + chipH * 0.25f;
        }
    }

    private static void log(String message) {
        if (DEBUG) {
            Log.d(TAG, message);
        }
    }
}
