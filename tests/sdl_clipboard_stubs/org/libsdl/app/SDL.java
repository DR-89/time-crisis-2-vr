package org.libsdl.app;

import android.content.Context;

public final class SDL {
    private static Context context;

    public static Context getContext() {
        return context;
    }

    public static void setContext(Context value) {
        context = value;
    }
}
