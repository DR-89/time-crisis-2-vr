package org.timecrisis2.arcadevr;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import java.io.File;

/** RESTART APP (the pause menu): runs in its own process, waits for the game's process to end after its clean shutdown
 *  (quest_host.c relaunch), then opens the game again exactly as the launcher does. */
public final class RestartActivity extends Activity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        final int pid = getIntent().getIntExtra("pid", -1);
        new Thread(() -> {
            File proc = new File("/proc/" + pid);
            for (int i = 0; i < 80 && pid > 0 && proc.exists(); i++) {
                try { Thread.sleep(100); } catch (InterruptedException e) { break; }
            }
            if (pid > 0 && proc.exists()) { Log.w("TCVR", "restart: the game is still running after 8 s, ending it"); android.os.Process.killProcess(pid); }
            runOnUiThread(() -> {
                startActivity(new Intent(this, MainActivity.class)
                    .setAction(Intent.ACTION_MAIN)
                    .addCategory("org.khronos.openxr.intent.category.IMMERSIVE_HMD")
                    .addCategory("com.oculus.intent.category.VR")
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                finish();
                System.exit(0);
            });
        }, "restart").start();
    }
}
