package android.content.res;
import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
public final class AssetFileDescriptor implements Closeable {
    public final CountDownLatch creating=new CountDownLatch(1),closed=new CountDownLatch(1);
    private final InputStream stream;
    private final boolean blockCreate;
    public AssetFileDescriptor(InputStream stream,boolean blockCreate){this.stream=stream;this.blockCreate=blockCreate;}
    public InputStream createInputStream()throws IOException {
        creating.countDown();
        if(blockCreate){try{closed.await(3,TimeUnit.SECONDS);}catch(InterruptedException ignored){/* model provider that needs descriptor close */}throw new IOException("cancelled creation");}
        return stream;
    }
    public void close()throws IOException{try{stream.close();}finally{closed.countDown();}}
}
