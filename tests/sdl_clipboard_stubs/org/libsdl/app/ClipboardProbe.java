package org.libsdl.app;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;

public final class ClipboardProbe {
    private static final class TestContext extends Context {
        private final Object service;

        TestContext(Object service) {
            this.service = service;
        }

        @Override
        public Object getSystemService(String name) {
            if (!CLIPBOARD_SERVICE.equals(name)) {
                throw new AssertionError("wrong service: " + name);
            }
            return service;
        }
    }

    public static void main(String[] args) {
        SDL.setContext(null);
        SDLClipboardHandler noContext = new SDLClipboardHandler();
        if (noContext.clipboardHasText() || !"".equals(noContext.clipboardGetText())) {
            throw new AssertionError("missing context must yield empty clipboard behavior");
        }
        noContext.clipboardSetText("ignored");
        noContext.onPrimaryClipChanged();
        if (SDLActivity.clipboardCallbacks != 0) {
            throw new AssertionError("missing service fired a native clipboard callback");
        }

        SDL.setContext(new TestContext(null));
        SDLClipboardHandler absent = new SDLClipboardHandler();
        if (absent.clipboardHasText()) {
            throw new AssertionError("missing service reported clipboard text");
        }
        if (!"".equals(absent.clipboardGetText())) {
            throw new AssertionError("missing service must return empty text");
        }
        absent.clipboardSetText("ignored");
        absent.onPrimaryClipChanged();
        if (SDLActivity.clipboardCallbacks != 0) {
            throw new AssertionError("absent service fired a native clipboard callback");
        }

        ClipboardManager manager = new ClipboardManager();
        SDL.setContext(new TestContext(manager));
        SDLClipboardHandler present = new SDLClipboardHandler();
        if (manager.addCalls != 1 || present.clipboardHasText()) {
            throw new AssertionError("listener setup or empty clipboard behavior changed");
        }
        if (present.clipboardGetText() != null) {
            throw new AssertionError("empty present clipboard behavior changed");
        }
        manager.setPrimaryClip(ClipData.newPlainText(null, "existing"));
        if (!present.clipboardHasText() || !"existing".equals(present.clipboardGetText())) {
            throw new AssertionError("existing clipboard text was not read");
        }
        present.clipboardSetText("updated");
        if (!"updated".equals(present.clipboardGetText()) || manager.removeCalls != 1 ||
                manager.addCalls != 2 || manager.setCalls != 2) {
            throw new AssertionError("existing clipboard set behavior changed");
        }
        manager.firePrimaryClipChanged();
        if (SDLActivity.clipboardCallbacks != 1) {
            throw new AssertionError("present service callback changed");
        }
        System.out.println("PASS: absent clipboard service is safe; present-service reads, writes, and callbacks are unchanged");
    }
}
