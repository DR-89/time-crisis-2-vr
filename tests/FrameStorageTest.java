import org.timecrisis2.arcadevr.RomInstaller;
import org.timecrisis2.frame.FrameStorage;
import java.io.*;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.*;

/** Exercises the production Frame importer and persistent-storage adapter. */
public final class FrameStorageTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    private static String sha(byte[] data) throws Exception {
        StringBuilder result = new StringBuilder();
        for (byte b : MessageDigest.getInstance("SHA-256").digest(data))
            result.append(String.format(Locale.ROOT, "%02x", b & 255));
        return result.toString();
    }
    private static void zip(File file, byte[][] chips, int count) throws Exception {
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(file))) {
            for (int i = 0; i < count; i++) {
                out.putNextEntry(new ZipEntry("renamed-entry-" + i + ".bin"));
                out.write(chips[i]);
                out.closeEntry();
            }
        }
    }
    private static void clean(Path root) throws IOException {
        try (java.util.stream.Stream<Path> paths = Files.walk(root)) {
            paths.sorted(Comparator.reverseOrder()).forEach(path -> {
                try { Files.delete(path); }
                catch (IOException e) { throw new UncheckedIOException(e); }
            });
        }
    }
    public static void main(String[] args) throws Exception {
        Path temp = Files.createTempDirectory("tc2-frame-storage-test-");
        try {
            byte[][] chips = new byte[22][];
            Map<String,String> expected = new LinkedHashMap<>();
            for (int i = 0; i < chips.length; i++) {
                chips[i] = ("tc2-frame-chip-" + i).getBytes("UTF-8");
                expected.put(String.format(Locale.ROOT, "chip-%02d.bin", i), sha(chips[i]));
            }
            File root = temp.resolve("Documents/TimeCrisis2VR").toFile();
            File candidate = new File(root, ".importing");
            check(!RomInstaller.ready(FrameStorage.roms(root), expected), "first launch requires ROM chips");
            try {
                FrameStorage.prepareRoms(root, Collections.singletonMap("one.bin", "0"));
                throw new AssertionError("incomplete manifest accepted");
            } catch (IOException correct) { }

            check(FrameStorage.ensureWritable(root).isDirectory(), "persistent root created and writable");
            zip(candidate, chips, chips.length);
            File roms = FrameStorage.importArchive(root, candidate, expected);
            check(RomInstaller.ready(roms, expected), "all 22 renamed hashes imported");
            check(FrameStorage.archive(root).isFile(), "validated archive installed at persistent root");

            String archiveBefore = sha(Files.readAllBytes(FrameStorage.archive(root).toPath()));
            zip(candidate, chips, 21);
            try {
                FrameStorage.importArchive(root, candidate, expected);
                throw new AssertionError("archive missing one chip accepted");
            } catch (IOException correct) { }
            check(archiveBefore.equals(sha(Files.readAllBytes(FrameStorage.archive(root).toPath()))),
                    "failed import does not replace the installed archive");
            check(RomInstaller.ready(roms, expected), "failed import preserves usable extracted data");

            Files.write(new File(roms, "chip-00.bin").toPath(), new byte[]{0});
            check(!RomInstaller.ready(roms, expected), "corrupt persistent chip detected");
            check(FrameStorage.prepareRoms(root, expected).equals(roms), "verified archive repairs extracted data");
            check(RomInstaller.ready(roms, expected), "archive repair restores all 22 chips");
            System.out.println("PASS: FrameStorage and RomInstaller production path; 22-chip validation, renamed ZIP entries, persistent archive, failed-import preservation, corruption repair");
        } finally { clean(temp); }
    }
}
