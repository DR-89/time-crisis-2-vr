package android.content;

public abstract class Context {
    public static final String CLIPBOARD_SERVICE = "clipboard";

    public abstract Object getSystemService(String name);
}
