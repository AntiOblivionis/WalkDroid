package org.walkdroid.audio.exclusive;
import android.system.Os;
import android.util.AtomicFile;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileInputStream;

/** Only system_server writes; a record never substitutes for a native reply. */
final class ReceiverHoldStore implements ReceiverHoldCoordinator.Store {
    private final AtomicFile file=new AtomicFile(new File("/data/system/walkdroid-receiver-hold"));
    private File backup(){return new File(file.getBaseFile().getPath()+".bak");}
    public ReceiverHoldCoordinator.Record load()throws Exception{
        if(!file.getBaseFile().exists()&&!backup().exists())return null;
        byte[] raw=new byte[1024];int size=0;try(FileInputStream in=file.openRead()){while(size<raw.length){int n=in.read(raw,size,raw.length-size);if(n<=0)break;size+=n;}}
        return ReceiverHoldCoordinator.Record.decode(new String(raw,0,size,"UTF-8"));
    }
    public void save(ReceiverHoldCoordinator.Record record)throws Exception{
        FileOutputStream out=null;
        byte[] expected=record.encode().getBytes("UTF-8");
        try{out=file.startWrite();out.write(expected);Os.fsync(out.getFD());file.finishWrite(out);out=null;}
        finally{if(out!=null)file.failWrite(out);}
    }
    public void remove()throws Exception{
        file.delete();
    }
}
