package org.libsdl.app;

public final class SDLActivity {
    public static int clipboardCallbacks;

    public static void onNativeClipboardChanged() {
        clipboardCallbacks++;
    }
}
