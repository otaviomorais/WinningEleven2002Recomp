package com.otaviomorais.sheepraider.hud;

/**
 * JNI bridge between the touch overlay (Java) and the engine.
 */
public final class HudBridge {
    public static final int CONTEXT_HIDDEN = 0;
    public static final int CONTEXT_ON_FOOT = 1;
    public static final int CONTEXT_CLIMBING = 2;
    public static final int CONTEXT_MENU = 3;

    // PS1 Gamepad Buttons
    public static final int BTN_CROSS = 0;
    public static final int BTN_CIRCLE = 1;
    public static final int BTN_SQUARE = 2;
    public static final int BTN_TRIANGLE = 3;
    public static final int BTN_L1 = 4;
    public static final int BTN_R1 = 5;
    public static final int BTN_SELECT = 6;
    public static final int BTN_START = 7;
    public static final int BTN_L2 = 8;
    public static final int BTN_R2 = 9;

    // Compatibility aliases
    public static final int BTN_A = BTN_CROSS;
    public static final int BTN_B = BTN_CIRCLE;
    public static final int BTN_X = BTN_SQUARE;
    public static final int BTN_Y = BTN_TRIANGLE;
    public static final int BTN_LB = BTN_L1;
    public static final int BTN_RB = BTN_R1;

    // Gamepad Axis
    public static final int AXIS_LEFT_X = 0;
    public static final int AXIS_LEFT_Y = 1;

    static {
        System.loadLibrary("sheepraider");
    }

    private HudBridge() {}

    public static native void nativePostButton(int button, boolean down);

    public static native void nativePostAxis(int axis, float value);

    public static native void nativePostConnected(boolean connected);

    public static native void nativeSetPhysicalGamepad(boolean connected);

    public static native void nativeSetGhostPadDeviceIds(int[] deviceIds);

    public static native int nativePollContext();
}
