package org.timecrisis2.arcadevr;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.widget.*;
import org.timecrisis2.frame.FrameStorage;
import java.io.*;
import java.util.Map;

/** Setup is a separate activity: SDL/OpenXR starts only after complete ROM validation. */
public final class LauncherActivity extends Activity {
    private TextView status;
    private Button choose, retry;
    private boolean frameStorageProbeFailed;
    private static final int PICK_ROM = 17;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this); layout.setOrientation(LinearLayout.VERTICAL); layout.setPadding(40,40,40,40);
        boolean frame = isFrame();
        TextView title = new TextView(this); title.setText(frame ? "Time Crisis II VR - Steam Frame Setup" : "Time Crisis II VR - Setup"); title.setTextSize(26); layout.addView(title);
        status = new TextView(this); status.setTextSize(18); status.setPadding(0,24,0,24); layout.addView(status);
        choose = new Button(this); choose.setText("Select your timecrs2.zip"); layout.addView(choose);
        retry = new Button(this); retry.setText("Check files again"); layout.addView(retry);
        choose.setOnClickListener(v -> {
            try { startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE), PICK_ROM); }
            catch (Exception e) { showMissing("No file picker available. Copy the ZIP using Install-ROM.ps1 on your PC."); }
        });
        retry.setOnClickListener(v -> {
            if (frame && frameStorageProbeFailed && !hasSharedStorageAccess()) openStorageSettings();
            else prepare(null);
        }); setContentView(layout); prepare(null);
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == PICK_ROM && result == RESULT_OK && data != null && data.getData() != null) prepare(data.getData());
    }
    private void showMissing(String detail) {
        Log.i("TCVR-ROM", detail);
        status.setText(detail + (isFrame()
            ? "\n\nGame files are not included. Select your own verified timecrs2.zip or copy it to /sdcard/Documents/TimeCrisis2VR/timecrs2.zip."
            : "\n\nGame files are not included. Supply your own Time Crisis II US TSS3 Ver.B ROM set (timecrs2.zip).\n\nYou can also use Install-ROM.ps1 via USB. No game files are downloaded by this app."));
        choose.setEnabled(true); retry.setEnabled(true);
    }
    private boolean isFrame() { return "org.timecrisis2.frame".equals(getPackageName()); }
    private boolean hasSharedStorageAccess() {
        return Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager();
    }
    private void openStorageSettings() {
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName())));
        } catch (Exception e) {
            showMissing("Open Android Settings and allow file access for Time Crisis II VR. " + e.getMessage());
        }
    }
    private void prepare(Uri selected) {
        choose.setEnabled(false); retry.setEnabled(false); status.setText("Checking and preparing your game files...");
        new Thread(() -> {
            File imported = null, frameCandidate = null;
            try {
                Map<String,String> expected = RomInstaller.manifest(getAssets().open("roms.sha256"));
                if (isFrame()) {
                    File root;
                    try {
                        root = FrameStorage.ensureWritable(FrameStorage.ROOT);
                        frameStorageProbeFailed = false;
                    } catch (IOException storageFailure) {
                        runOnUiThread(() -> {
                            frameStorageProbeFailed = true;
                            showMissing("Cannot write the persistent game folder. Retry to open storage settings if needed. " + storageFailure.getMessage());
                        });
                        return;
                    }
                    if (selected != null) {
                        frameCandidate = new File(root, ".timecrs2.zip.importing");
                        File candidate = frameCandidate;
                        try (InputStream in = getContentResolver().openInputStream(selected); FileOutputStream out = new FileOutputStream(candidate)) {
                            if (in == null) throw new IOException("Cannot open selected ZIP");
                            byte[] buffer = new byte[65536]; int n; long total = 0;
                            while ((n = in.read(buffer)) != -1) {
                                total += n; if (total > 128L*1024*1024) throw new IOException("ZIP exceeds 128 MiB limit");
                                out.write(buffer,0,n);
                            }
                            out.getFD().sync();
                        }
                        FrameStorage.importArchive(root, candidate, expected);
                    } else {
                        FrameStorage.prepareRoms(root, expected);
                    }
                    Log.i("TCVR-ROM", "Frame ROMs verified; starting VR");
                    runOnUiThread(() -> {
                        if (isFinishing() || isDestroyed()) return;
                        startActivity(new Intent(this, MainActivity.class).setAction(Intent.ACTION_MAIN)
                            .addCategory("org.khronos.openxr.intent.category.IMMERSIVE_HMD")
                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                        finish();
                    });
                    return;
                }
                File directory = new File(getFilesDir(), "extracted");
                if (selected != null) {
                    imported = new File(getCacheDir(), "selected-timecrs2.zip");
                    try (InputStream in = getContentResolver().openInputStream(selected); FileOutputStream out = new FileOutputStream(imported)) {
                        if (in == null) throw new IOException("Cannot open selected ZIP");
                        byte[] buffer = new byte[65536]; int n; long total = 0;
                        while ((n = in.read(buffer)) != -1) {
                            total += n; if (total > 128L*1024*1024) throw new IOException("ZIP exceeds 128 MiB limit");
                            out.write(buffer,0,n);
                        }
                    }
                    RomInstaller.importZip(imported, directory, expected);
                } else if (!RomInstaller.ready(directory, expected)) {
                    // Private builds can contain the user's own ROMs. The public
                    // build has no such directory and continues with ZIP import.
                    String[] bundled = getAssets().list("roms");
                    if (bundled != null && bundled.length > 0) {
                        if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Cannot create ROM directory");
                        for (String name : expected.keySet()) {
                            File temp = new File(directory, name + ".tmp");
                            try (InputStream in = getAssets().open("roms/" + name); FileOutputStream out = new FileOutputStream(temp)) {
                                byte[] buffer = new byte[65536]; int n;
                                while ((n = in.read(buffer)) != -1) out.write(buffer, 0, n);
                                out.getFD().sync();
                            }
                            java.nio.file.Files.move(temp.toPath(), new File(directory,name).toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                        }
                        if (!RomInstaller.ready(directory, expected)) throw new IOException("Bundled ROM verification failed");
                    } else {
                    File external = getExternalFilesDir(null);
                    File archive = external == null ? null : new File(external, "timecrs2.zip");
                    if (archive == null || !archive.isFile()) throw new IOException("Your ROM set is needed before the first start.");
                    RomInstaller.importZip(archive, directory, expected);
                    }
                }
                Log.i("TCVR-ROM", "ROMs verified; starting VR");
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    startActivity(new Intent(this, MainActivity.class)
                        .setAction(Intent.ACTION_MAIN)
                        .addCategory("org.khronos.openxr.intent.category.IMMERSIVE_HMD")
                        .addCategory("com.oculus.intent.category.VR")
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                    finish();
                });
            } catch (Exception e) {
                Log.w("TCVR-ROM", "Setup: " + e.getMessage());
                runOnUiThread(() -> { if (!isFinishing() && !isDestroyed()) showMissing(e.getMessage()); });
            } finally {
                if (imported != null) imported.delete();
                if (frameCandidate != null) frameCandidate.delete();
            }
        }, "ROM-import").start();
    }
}
