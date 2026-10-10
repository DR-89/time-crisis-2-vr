package org.timecrisis2.arcadevr;

import java.io.*;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import java.util.zip.*;

/** Imports only checksum-matched chip files, never ZIP paths. No Android dependency. */
public final class RomInstaller {
    private static final int MAX_CHIP = 8 * 1024 * 1024;
    public static Map<String,String> manifest(InputStream stream) throws IOException {
        Map<String,String> result = new LinkedHashMap<>();
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(stream, "UTF-8"))) {
            String line;
            while ((line = reader.readLine()) != null) {
                String[] parts = line.split("  ", 2);
                if (parts.length != 2 || !parts[0].matches("[0-9a-f]{64}") ||
                    !parts[1].matches("[a-zA-Z0-9_-]+\\.[a-zA-Z0-9_-]+") || result.containsKey(parts[1]))
                    throw new IOException("Invalid ROM manifest");
                result.put(parts[1], parts[0]);
            }
        }
        if (result.isEmpty()) throw new IOException("Empty ROM manifest");
        return result;
    }
    private static String digest(InputStream in) throws IOException {
        try {
            MessageDigest sha = MessageDigest.getInstance("SHA-256");
            byte[] buffer = new byte[65536]; int n, total = 0;
            while ((n = in.read(buffer)) != -1) {
                total += n;
                if (total > MAX_CHIP) throw new IOException("ROM chip exceeds size limit");
                sha.update(buffer, 0, n);
            }
            StringBuilder out = new StringBuilder();
            for (byte b : sha.digest()) out.append(String.format(Locale.ROOT, "%02x", b & 255));
            return out.toString();
        } catch (NoSuchAlgorithmException e) { throw new IOException(e); }
    }
    public static boolean ready(File directory, Map<String,String> expected) throws IOException {
        for (Map.Entry<String,String> item : expected.entrySet()) {
            File file = new File(directory, item.getKey());
            if (!file.isFile() || file.length() > MAX_CHIP) return false;
            try (InputStream in = new FileInputStream(file)) {
                if (!digest(in).equals(item.getValue())) return false;
            }
        }
        return true;
    }
    public static void importZip(File archive, File directory, Map<String,String> expected) throws IOException {
        try (ZipFile zip = new ZipFile(archive)) {
            Map<String,ZipEntry> matches = new HashMap<>();
            Set<String> wanted = new HashSet<>(expected.values());
            Enumeration<? extends ZipEntry> entries = zip.entries(); int count = 0;
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                if (++count > 4096) throw new IOException("Too many ZIP entries");
                String name = entry.getName();
                if (entry.isDirectory() || name.contains("/") || name.contains("\\") ||
                    entry.getSize() < 0 || entry.getSize() > MAX_CHIP) continue;
                try (InputStream in = zip.getInputStream(entry)) {
                    String hash = digest(in);
                    if (wanted.contains(hash)) matches.put(hash, entry);
                }
            }
            for (Map.Entry<String,String> item : expected.entrySet())
                if (!matches.containsKey(item.getValue()))
                    throw new IOException("Missing or incorrect " + item.getKey() + ". Use Time Crisis II US TSS3 Ver.B (timecrs2).");
            if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Cannot create ROM directory");
            for (Map.Entry<String,String> item : expected.entrySet()) {
                File temp = new File(directory, item.getKey() + ".tmp");
                try {
                    try (InputStream in = zip.getInputStream(matches.get(item.getValue())); FileOutputStream out = new FileOutputStream(temp)) {
                        byte[] buffer = new byte[65536]; int n, total = 0;
                        while ((n = in.read(buffer)) != -1) {
                            total += n; if (total > MAX_CHIP) throw new IOException("ROM chip exceeds size limit");
                            out.write(buffer, 0, n);
                        }
                        out.getFD().sync();
                    }
                    try (InputStream in = new FileInputStream(temp)) {
                        if (!digest(in).equals(item.getValue())) throw new IOException("ROM changed during import");
                    }
                    Files.move(temp.toPath(), new File(directory, item.getKey()).toPath(), StandardCopyOption.REPLACE_EXISTING);
                } finally { temp.delete(); }
            }
        }
        if (!ready(directory, expected)) throw new IOException("ROM verification failed");
    }
}
