package org.timecrisis2.arcadevr;
import org.libsdl.app.SDLActivity;
import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.media.audiofx.AcousticEchoCanceler;
import android.media.audiofx.NoiseSuppressor;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import java.io.*;
import java.security.MessageDigest;

public final class MainActivity extends SDLActivity {
    @Override protected String[] getLibraries() { return new String[]{"SDL2", "openxr_loader", "main"}; }
    @Override public void onCreate(Bundle state) {
        try {
            // The bundled APK launches this VR activity directly, just as the
            // original working Quest build did. Prepare assets before SDL starts.
            if(!RomInstaller.ready(new File(getFilesDir(),"extracted"),RomInstaller.manifest(getAssets().open("roms.sha256"))))
                installAssets("roms");
            installAssets("models");
        }
        catch(Exception e) { throw new IllegalStateException("Game asset installation failed", e); }
        // Online play: a Quest hosting a game must receive the LAN discovery broadcasts of the partner's
        // "Find LAN games"; Android filters broadcast/multicast on Wi-Fi without this lock.
        try {
            WifiManager wifi=(WifiManager)getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if(wifi!=null){multicast=wifi.createMulticastLock("tc2vr-lan-discovery");multicast.setReferenceCounted(false);multicast.acquire();}
        } catch(Exception e) { multicast=null; }
        super.onCreate(state);
    }
    private WifiManager.MulticastLock multicast;
    @Override protected void onDestroy() {
        if(multicast!=null&&multicast.isHeld())multicast.release();
        voiceStop();
        super.onDestroy();
    }

    // ---- Online voice chat (quest/tc2_voice.c). The native side calls voiceStart/voiceStop on its own thread. ----
    // VOICE_COMMUNICATION engages the platform's echo cancellation for the headset's speakers, which plain capture lacks.
    private static native void nativeVoicePcm(short[] pcm,int samples);
    private static native void nativeVoiceState(int state);   // 1 on with echo cancellation, 2 on without, -1 refused, -2 failed
    private static final int VOICE_PERMISSION=7201;
    private volatile boolean voiceWanted,voiceRunning;
    private Thread voiceThread;
    public void voiceStart() {
        voiceWanted=true;
        if(checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED) {
            runOnUiThread(new Runnable(){public void run(){requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO},VOICE_PERMISSION);}});
            return;
        }
        startRecording();
    }
    public synchronized void voiceStop() {
        voiceWanted=false;voiceRunning=false;
        if(voiceThread!=null){try{voiceThread.join(500);}catch(InterruptedException e){}voiceThread=null;}
    }
    @Override public void onRequestPermissionsResult(int code,String[] permissions,int[] results) {
        if(code==VOICE_PERMISSION) {
            if(results.length>0&&results[0]==PackageManager.PERMISSION_GRANTED){if(voiceWanted)startRecording();}
            else nativeVoiceState(-1);
            return;
        }
        super.onRequestPermissionsResult(code,permissions,results);
    }
    private synchronized void startRecording() {
        if(voiceThread!=null)return;
        voiceRunning=true;
        voiceThread=new Thread(new Runnable(){public void run(){record();}},"tc2-voice");
        voiceThread.start();
    }
    private void record() {
        int rate=48000,min=AudioRecord.getMinBufferSize(rate,AudioFormat.CHANNEL_IN_MONO,AudioFormat.ENCODING_PCM_16BIT);
        AudioRecord rec;
        try{rec=new AudioRecord(MediaRecorder.AudioSource.VOICE_COMMUNICATION,rate,AudioFormat.CHANNEL_IN_MONO,AudioFormat.ENCODING_PCM_16BIT,Math.max(min,rate/5*2));}
        catch(Exception e){nativeVoiceState(-2);return;}
        if(rec.getState()!=AudioRecord.STATE_INITIALIZED){rec.release();nativeVoiceState(-2);return;}
        boolean echo=false;AcousticEchoCanceler aec=null;NoiseSuppressor ns=null;
        try{
            if(AcousticEchoCanceler.isAvailable()){aec=AcousticEchoCanceler.create(rec.getAudioSessionId());if(aec!=null){aec.setEnabled(true);echo=aec.getEnabled();}}
            if(NoiseSuppressor.isAvailable()){ns=NoiseSuppressor.create(rec.getAudioSessionId());if(ns!=null)ns.setEnabled(true);}
        }catch(Exception e){}
        rec.startRecording();
        nativeVoiceState(echo?1:2);
        short[] buf=new short[960];
        while(voiceRunning){int n=rec.read(buf,0,buf.length);if(n>0)nativeVoicePcm(buf,n);else if(n<0)break;}
        try{rec.stop();}catch(Exception e){}
        rec.release();if(aec!=null)aec.release();if(ns!=null)ns.release();
    }
    private void installAssets(String category) throws Exception {
        File dir=new File(getFilesDir(),category.equals("roms")?"extracted":category);
        if(!dir.isDirectory()&&!dir.mkdirs())throw new IOException("Cannot create ROM directory");
        // Every asset is checked before use, including after an APK update.
        try(BufferedReader reader=new BufferedReader(new InputStreamReader(getAssets().open(category+".sha256")))) {
            String line;
            while((line=reader.readLine())!=null) {
                String[] parts=line.split("  ",2);if(parts.length!=2)throw new IOException("Bad ROM manifest");
                String name=parts[1];if(name.contains("/")||name.contains("\\")||name.contains(".."))throw new IOException("Bad ROM name");
                File dest=new File(dir,name);
                if(dest.exists()&&digest(dest).equals(parts[0]))continue;
                File tmp=new File(dir,name+".tmp");
                try(InputStream in=getAssets().open(category+"/"+name);FileOutputStream out=new FileOutputStream(tmp)) {
                    byte[] buf=new byte[65536];int n;while((n=in.read(buf))!=-1)out.write(buf,0,n);
                    out.getFD().sync();
                }
                if(!digest(tmp).equals(parts[0]))throw new IOException("ROM checksum: "+name);
                if(!tmp.renameTo(dest))throw new IOException("Cannot install "+name);
            }
        }
    }
    private String digest(File file) throws Exception {
        MessageDigest d=MessageDigest.getInstance("SHA-256");
        try(InputStream in=new FileInputStream(file)){byte[] b=new byte[65536];int n;while((n=in.read(b))!=-1)d.update(b,0,n);}
        StringBuilder s=new StringBuilder();for(byte b:d.digest())s.append(String.format("%02x",b&255));return s.toString();
    }
}
