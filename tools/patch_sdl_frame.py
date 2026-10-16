"""Create an isolated Steam Frame SDL Java view with null-safe clipboard access."""
from pathlib import Path
import hashlib
import shutil

PATCH_ID = "tc2-frame-null-clipboard-v1"
HANDLER_PATH = Path("org/libsdl/app/SDLActivity.java")
PATCH_MARKER = "/* TC2_FRAME_NULL_CLIPBOARD_V1 */"

ORIGINAL_HANDLER = '''class SDLClipboardHandler implements
    ClipboardManager.OnPrimaryClipChangedListener {

    protected ClipboardManager mClipMgr;

    SDLClipboardHandler() {
       mClipMgr = (ClipboardManager) SDL.getContext().getSystemService(Context.CLIPBOARD_SERVICE);
       mClipMgr.addPrimaryClipChangedListener(this);
    }

    public boolean clipboardHasText() {
       return mClipMgr.hasPrimaryClip();
    }

    public String clipboardGetText() {
        ClipData clip = mClipMgr.getPrimaryClip();
        if (clip != null) {
            ClipData.Item item = clip.getItemAt(0);
            if (item != null) {
                CharSequence text = item.getText();
                if (text != null) {
                    return text.toString();
                }
            }
        }
        return null;
    }

    public void clipboardSetText(String string) {
       mClipMgr.removePrimaryClipChangedListener(this);
       ClipData clip = ClipData.newPlainText(null, string);
       mClipMgr.setPrimaryClip(clip);
       mClipMgr.addPrimaryClipChangedListener(this);
    }

    @Override
    public void onPrimaryClipChanged() {
        SDLActivity.onNativeClipboardChanged();
    }
}'''

PATCHED_HANDLER = '''/* TC2_FRAME_NULL_CLIPBOARD_V1 */
class SDLClipboardHandler implements
    ClipboardManager.OnPrimaryClipChangedListener {

    protected ClipboardManager mClipMgr;

    SDLClipboardHandler() {
       Context context = SDL.getContext();
       mClipMgr = context == null ? null : (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
       if (mClipMgr != null) {
           mClipMgr.addPrimaryClipChangedListener(this);
       }
    }

    public boolean clipboardHasText() {
       return mClipMgr != null && mClipMgr.hasPrimaryClip();
    }

    public String clipboardGetText() {
        if (mClipMgr == null) {
            return "";
        }
        ClipData clip = mClipMgr.getPrimaryClip();
        if (clip != null) {
            ClipData.Item item = clip.getItemAt(0);
            if (item != null) {
                CharSequence text = item.getText();
                if (text != null) {
                    return text.toString();
                }
            }
        }
        return null;
    }

    public void clipboardSetText(String string) {
       if (mClipMgr == null) {
           return;
       }
       mClipMgr.removePrimaryClipChangedListener(this);
       ClipData clip = ClipData.newPlainText(null, string);
       mClipMgr.setPrimaryClip(clip);
       mClipMgr.addPrimaryClipChangedListener(this);
    }

    @Override
    public void onPrimaryClipChanged() {
        if (mClipMgr != null) {
            SDLActivity.onNativeClipboardChanged();
        }
    }
}'''


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def java_tree_sha256(root):
    root = Path(root)
    digest = hashlib.sha256()
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        digest.update(path.relative_to(root).as_posix().encode("utf-8") + b"\0")
        digest.update(path.read_bytes())
    return digest.hexdigest()


def patch_handler_file(path):
    path = Path(path)
    original = path.read_bytes()
    text = original.decode("utf-8")
    if PATCH_MARKER in text:
        if text.count(PATCHED_HANDLER) != 1:
            raise RuntimeError(f"Frame SDL clipboard patch marker has unexpected content: {path}")
        return {"source_sha256": None, "patched_sha256": sha256(original)}
    if text.count(ORIGINAL_HANDLER) != 1:
        raise RuntimeError(f"Pinned SDLClipboardHandler source did not match SDL 2.30.11: {path}")
    patched = text.replace(ORIGINAL_HANDLER, PATCHED_HANDLER)
    encoded = patched.encode("utf-8")
    path.write_bytes(encoded)
    return {"source_sha256": sha256(original), "patched_sha256": sha256(encoded)}


def prepare_frame_java_view(source_root, destination_root):
    source_root = Path(source_root).resolve()
    destination_root = Path(destination_root).resolve()
    if (source_root == destination_root or source_root in destination_root.parents or
            destination_root in source_root.parents):
        raise ValueError("Frame SDL Java view must be separate from its pinned source tree")
    if destination_root.exists():
        shutil.rmtree(destination_root)
    shutil.copytree(source_root, destination_root)
    metadata = patch_handler_file(destination_root / HANDLER_PATH)
    metadata.update({
        "patch_id": PATCH_ID,
        "source_handler": HANDLER_PATH.as_posix(),
        "generated_java_tree_sha256": java_tree_sha256(destination_root),
    })
    return metadata
