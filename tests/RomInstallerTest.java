import org.timecrisis2.arcadevr.RomInstaller;
import java.io.*;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import java.util.zip.*;

public final class RomInstallerTest {
    static void check(boolean ok,String message){if(!ok)throw new AssertionError(message);}
    static String sha(byte[] data)throws Exception{
        StringBuilder out=new StringBuilder();for(byte b:MessageDigest.getInstance("SHA-256").digest(data))out.append(String.format("%02x",b&255));return out.toString();
    }
    static void zip(File file,String name,byte[] data)throws Exception{
        try(ZipOutputStream out=new ZipOutputStream(new FileOutputStream(file))){out.putNextEntry(new ZipEntry(name));out.write(data);out.closeEntry();}
    }
    public static void main(String[] args)throws Exception{
        Path temp=Files.createTempDirectory("tcvr-rom-test-");
        try{
            byte[] data={1,3,7,9};Map<String,String> expected=new LinkedHashMap<>();expected.put("chip.1",sha(data));
            File dir=temp.resolve("roms").toFile(),archive=temp.resolve("rom.zip").toFile();
            check(!RomInstaller.ready(dir,expected),"Missing ROMs cannot start");
            zip(archive,"legacy-name.bin",data);RomInstaller.importZip(archive,dir,expected);
            check(RomInstaller.ready(dir,expected),"Hash-matched renamed chip imported");
            Files.write(dir.toPath().resolve("chip.1"),new byte[]{0});check(!RomInstaller.ready(dir,expected),"Corruption detected");
            RomInstaller.importZip(archive,dir,expected);check(RomInstaller.ready(dir,expected),"Repair works");
            zip(archive,"../chip.1",data);
            try{RomInstaller.importZip(archive,dir,expected);throw new AssertionError("Traversal entry accepted");}catch(IOException correct){}
            check(RomInstaller.ready(dir,expected),"Failed import preserves existing data");
            zip(archive,"chip.1",new byte[]{0});
            try{RomInstaller.importZip(archive,dir,expected);throw new AssertionError("Wrong ROM accepted");}catch(IOException correct){}
            try{RomInstaller.manifest(new ByteArrayInputStream((sha(data)+"  ../bad\n").getBytes("UTF-8")));throw new AssertionError("Bad manifest accepted");}catch(IOException correct){}
            if(args.length==2){
                Map<String,String> real=RomInstaller.manifest(new FileInputStream(args[1]));
                File game=temp.resolve("game-roms").toFile();RomInstaller.importZip(new File(args[0]),game,real);
                check(real.size()==31&&RomInstaller.ready(game,real),"All real game ROMs verified");
            }
            System.out.println("PASS: first-run, renamed chips, corruption, repair, invalid ZIP contents, traversal, manifest validation and real ROM import.");
        }finally{
            try(java.util.stream.Stream<Path> paths=Files.walk(temp)){paths.sorted(Comparator.reverseOrder()).forEach(p->{try{Files.delete(p);}catch(IOException e){throw new UncheckedIOException(e);}});}
        }
    }
}
