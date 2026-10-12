package app.mmfpocket.player;

import android.content.Context;
import android.content.ContentResolver;
import android.content.res.AssetFileDescriptor;
import android.net.Uri;
import android.os.CancellationSignal;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;

/** Actual analysis coordinator with cooperative provider doubles, not Android/provider timing. */
public final class MafuAnalysisLifecycleTest {
    static void check(boolean value,String message){if(!value)throw new AssertionError(message);}
    static void await(CountDownLatch latch)throws Exception{check(latch.await(3,TimeUnit.SECONDS),"lifecycle barrier timeout");}
    static void until(BooleanSupplier condition)throws Exception{
        long deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(3);
        while(!condition.getAsBoolean()&&System.nanoTime()<deadline)Thread.sleep(1);
        check(condition.getAsBoolean(),"next source analysis stalled");
    }
    static void scenario(int mode)throws Exception {
        byte[] wav=MafuAudioTest.wav(16,1,true,false);
        CountDownLatch entered=new CountDownLatch(1),cancelled=new CountDownLatch(1),readClosed=new CountDownLatch(1);
        AtomicReference<AssetFileDescriptor> blockedDescriptor=new AtomicReference<>();
        InputStream blockedRead=new InputStream(){
            public int read()throws IOException{entered.countDown();try{readClosed.await(3,TimeUnit.SECONDS);}catch(InterruptedException ignored){}throw new IOException("closed visual read");}
            public void close(){readClosed.countDown();}
        };
        ContentResolver resolver=new ContentResolver(){
            public AssetFileDescriptor openAssetFileDescriptor(Uri uri,String access,CancellationSignal signal)throws IOException {
                if(uri.toString().equals("next"))return new AssetFileDescriptor(new ByteArrayInputStream(wav),false);
                signal.setOnCancelListener(()->{cancelled.countDown();if(mode==3)throw new IllegalStateException("injected cancel failure");});
                if(mode==0){entered.countDown();try{cancelled.await(3,TimeUnit.SECONDS);}catch(InterruptedException ignored){}throw new IOException("cancelled visual open");}
                AssetFileDescriptor descriptor=new AssetFileDescriptor(mode==2||mode==3?blockedRead:new ByteArrayInputStream(wav),mode==1);
                blockedDescriptor.set(descriptor);
                return descriptor;
            }
        };
        MafuAudioAnalysis analysis=new MafuAudioAnalysis(new Context(resolver));
        MafuAudioVisualization.Source old=MafuAudioVisualization.start("same");
        MafuAudioVisualization.Source next=null;
        try {
            analysis.start(old,null,Uri.parse("old"));
            if(mode==1){until(()->blockedDescriptor.get()!=null);await(blockedDescriptor.get().creating);}else await(entered);
            MafuAudioVisualization.clear(old);next=MafuAudioVisualization.start("same");
            MafuAudioVisualization.Source current=next;
            analysis.start(next,null,Uri.parse("next"));
            until(()->current.envelope.size()>=5);
            await(cancelled);if(mode==2||mode==3)await(readClosed);
            float[] value=new float[4];check(next.sample(80,value)&&value[0]>.5,"new source publishes actual measured PCM");
            check(MafuAudioVisualization.find("same",next.token)==next&&MafuAudioVisualization.find("same",old.token)==null,"old callback cannot contaminate identical next source");
            if(mode==1)await(blockedDescriptor.get().closed);
        }finally{MafuAudioVisualization.clear(old);MafuAudioVisualization.clear(next);analysis.release();}
    }
    public static void main(String[] args)throws Exception {
        for(int mode=0;mode<4;mode++)scenario(mode);
        System.out.println("PASS4 actual analysis coordinator cases: cancellable open, descriptor creation/read close, exceptional cancel still closes; newer source progress/identity. Cooperative host doubles only, not real SAF guarantees.");
    }
}
