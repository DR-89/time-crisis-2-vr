package android.content;

public final class ClipData {
    private final Item item;

    private ClipData(CharSequence text) {
        item = new Item(text);
    }

    public static ClipData newPlainText(CharSequence label, CharSequence text) {
        return new ClipData(text);
    }

    public Item getItemAt(int index) {
        return index == 0 ? item : null;
    }

    public static final class Item {
        private final CharSequence text;

        private Item(CharSequence text) {
            this.text = text;
        }

        public CharSequence getText() {
            return text;
        }
    }
}
