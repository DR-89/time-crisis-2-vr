package org.timecrisis2.arcadevr;
import org.libsdl.app.SDLActivity;
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
        super.onCreate(state);
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
