package android.content;

import java.util.ArrayList;
import java.util.List;

public class ClipboardManager {
    public interface OnPrimaryClipChangedListener {
        void onPrimaryClipChanged();
    }

    private final List<OnPrimaryClipChangedListener> listeners = new ArrayList<>();
    private ClipData clip;
    public int addCalls;
    public int removeCalls;
    public int setCalls;

    public void addPrimaryClipChangedListener(OnPrimaryClipChangedListener listener) {
        addCalls++;
        listeners.add(listener);
    }

    public void removePrimaryClipChangedListener(OnPrimaryClipChangedListener listener) {
        removeCalls++;
        listeners.remove(listener);
    }

    public boolean hasPrimaryClip() {
        return clip != null;
    }

    public ClipData getPrimaryClip() {
        return clip;
    }

    public void setPrimaryClip(ClipData value) {
        setCalls++;
        clip = value;
    }

    public void firePrimaryClipChanged() {
        for (OnPrimaryClipChangedListener listener : new ArrayList<>(listeners)) {
            listener.onPrimaryClipChanged();
        }
    }
}
