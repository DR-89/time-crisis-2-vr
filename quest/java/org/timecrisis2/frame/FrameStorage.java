package org.timecrisis2.frame;

import org.timecrisis2.arcadevr.RomInstaller;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Map;

/** Persistent files shared by the Steam Frame launcher and native game process. */
public final class FrameStorage {
    public static final File ROOT = new File("/sdcard/Documents/TimeCrisis2VR");
    private FrameStorage() {}

    public static File archive(File root) { return new File(root, "timecrs2.zip"); }
    public static File roms(File root) { return new File(root, "extracted"); }
    public static File models(File root) { return new File(root, "models"); }

    public static File ensureWritable(File root) throws IOException {
        if (!root.isDirectory() && !root.mkdirs())
            throw new IOException("Cannot create persistent storage directory: " + root);
        File probe = null;
        try {
            probe = File.createTempFile(".tc2-write-", ".tmp", root);
        } catch (IOException e) {
            throw new IOException("Cannot write " + root + "; allow shared storage access", e);
        } finally {
            if (probe != null && probe.exists() && !probe.delete()) probe.deleteOnExit();
        }
        return root;
    }

    /** Import by the pinned 22-chip hash manifest; filenames in the ZIP are not trusted. */
    public static File prepareRoms(File root, Map<String,String> expected) throws IOException {
        if (expected.size() != 22) throw new IOException("Expected the 22-chip Time Crisis II ROM manifest");
        ensureWritable(root);
        File directory = roms(root);
        if (RomInstaller.ready(directory, expected)) return directory;
        File zip = archive(root);
        if (!zip.isFile())
            throw new IOException("Missing " + zip + ". Copy your own timecrs2.zip there, then retry.");
        try {
            RomInstaller.importZip(zip, directory, expected);
        } catch (IOException e) {
            throw new IOException("Could not import " + zip + ": " + e.getMessage(), e);
        }
        return directory;
    }

    /** Validate and import a selected archive before replacing the persistent copy. */
    public static File importArchive(File root, File candidate, Map<String,String> expected) throws IOException {
        if (expected.size() != 22) throw new IOException("Expected the 22-chip Time Crisis II ROM manifest");
        ensureWritable(root);
        File directory = roms(root);
        try {
            RomInstaller.importZip(candidate, directory, expected);
            Files.move(candidate.toPath(), archive(root).toPath(), StandardCopyOption.REPLACE_EXISTING);
        } catch (IOException e) {
            throw new IOException("Could not import " + candidate + ": " + e.getMessage(), e);
        }
        return directory;
    }
}
