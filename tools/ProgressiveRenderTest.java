// SPDX-License-Identifier: Apache-2.0
// Actual ProgressivePlayback renderLoop, host-only PCM producer/device doubles.
// No AudioTrack, playbackLoop, JNI, MMF decoding, threads or device are exercised.
package app.mmfpocket.player;
import java.io.File;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.HexFormat;

public final class ProgressiveRenderTest {
    static void check(boolean ok,String reason){if(!ok)throw new AssertionError(reason);}
    static final class Events implements ProgressivePlayback.Listener {
        int ready,cache,completed,errors;String error="";File saved;
        public void onReady(){++ready;}
        public void onCacheReady(File file){++cache;saved=file;}
        public void onCompleted(){++completed;}
        public void onError(String text){++errors;error=text;}
    }
    static void runRender(ProgressivePlayback player)throws Exception {
        Method method=ProgressivePlayback.class.getDeclaredMethod("renderLoop");
        method.setAccessible(true);method.invoke(player);
    }
    static short[] fixture(int type){
        if(type==0)return new short[0];
        short[] pcm=new short[1037*2];
        for(int i=0;i<pcm.length;++i)pcm[i]=switch(type){
            case 1 -> 0;
            case 2 -> 1;
            case 3 -> -1;
            case 4 -> (short)(i%2==0?2:-2);
            case 5 -> (short)(i%2==0?3:-3);
            case 6 -> (short)(i%2==0?4:-4);
            case 7 -> (short)(i%2==0?32767:-32768);
            case 8 -> (short)(i<2048?0:i%2==0?1:-1);
            case 9 -> (short)(i<2000?1:i%2==0?320:-320);
            default -> throw new AssertionError("Unknown fixture");
        };
        return pcm;
    }
    static byte[] oracle(short[] samples){
        ByteBuffer result=ByteBuffer.allocate(44+samples.length*2).order(ByteOrder.LITTLE_ENDIAN);
        result.put("RIFF".getBytes(StandardCharsets.US_ASCII)).putInt(result.capacity()-8);
        result.put("WAVEfmt ".getBytes(StandardCharsets.US_ASCII)).putInt(16);
        result.putShort((short)1).putShort((short)2).putInt(44100).putInt(44100*4);
        result.putShort((short)4).putShort((short)16);
        result.put("data".getBytes(StandardCharsets.US_ASCII)).putInt(samples.length*2);
        for(short sample:samples)result.putShort(sample);
        return result.array();
    }
    static String hash(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    static void condition(Path root,int type,int block,boolean phone)throws Exception {
        short[] pcm=fixture(type);boolean nonzero=false;
        for(short sample:pcm)nonzero|=sample!=0;
        Path folder=Files.createTempDirectory(root,"case-");
        File partial=folder.resolve("render.part").toFile(),cache=folder.resolve("cache.wav").toFile();
        Events events=new Events();NativeMmfRenderer.configure(pcm,block,NativeMmfRenderer.Failure.NONE);
        ProgressivePlayback player=new ProgressivePlayback(new byte[]{1},phone,partial,cache,events);
        runRender(player);
        check(NativeMmfRenderer.creates==1&&NativeMmfRenderer.destroys==1,"session destroyed once");
        check(NativeMmfRenderer.renders==(pcm.length/2+block-1)/block+1,"PCM blocks and EOF count exact");
        check(NativeMmfRenderer.lastPhone==phone,"phone flag passed unchanged");
        check(player.getRenderedFrames()==pcm.length/2,"all PCM frames retained");
        check(!partial.exists(),"partial moved or removed");
        check(events.ready==0&&events.completed==0,"render-only test never exercises playback/device callbacks");
        String savedHash="none";
        if(nonzero){
            check(events.errors==0&&events.cache==1&&cache.equals(events.saved)&&cache.isFile(),
                  "representable quiet PCM must finalize progressive cache without error");
            check(player.isRenderingComplete()&&player.getTimelineFrames()==pcm.length/2,"completed exact timeline");
            byte[] saved=Files.readAllBytes(cache.toPath());
            check(Arrays.equals(saved,oracle(pcm)),"entire cached WAV exact to independent PCM/header oracle, no normalization");
            savedHash=hash(saved);
        }else{
            check(events.errors==1&&events.error.equals("音声データを生成できませんでした")&&events.cache==0,
                  "zero/empty PCM still rejects exactly once");
            check(!cache.exists()&&!player.isRenderingComplete(),"silent cache never published");
        }
        System.out.println(type+"/"+block+"/"+phone+"\t"+(nonzero?"OK":"silent-rejected")+"\t"+pcm.length/2+"\t"+savedHash);
    }
    static void failure(Path root,NativeMmfRenderer.Failure fault,boolean cancel,boolean missingCacheParent)throws Exception {
        Path folder=Files.createTempDirectory(root,"fault-");
        File partial=folder.resolve("render.part").toFile();
        File cache=folder.resolve(missingCacheParent?"absent/cache.wav":"cache.wav").toFile();
        Events events=new Events();NativeMmfRenderer.configure(fixture(6),31,fault);
        ProgressivePlayback player=new ProgressivePlayback(new byte[]{1},false,partial,cache,events);
        if(cancel)player.cancel();runRender(player);
        check(NativeMmfRenderer.creates==1&&NativeMmfRenderer.destroys==(fault==NativeMmfRenderer.Failure.CREATE?0:1),"error/cancel native handle cleanup");
        check(!partial.exists()&&!cache.exists()&&events.cache==0&&!player.isRenderingComplete(),"error/cancel never publishes or leaves partial");
        check(events.errors==(cancel?0:1),"error once, cancellation quiet");
        if(!cancel)check(!events.error.isEmpty(),"explicit error text");
    }
    public static void main(String[] args)throws Exception {
        check(args.length>=1,"generated test folder required");
        Path root=Path.of(args[0]);Files.createDirectories(root);
        boolean legacyOnly=args.length==2&&args[1].equals("--legacy-only");int cases=0;
        for(int type=0;type<10;++type){
            if(legacyOnly&&(type>=2&&type<=5||type==8))continue;
            for(int block:new int[]{1,31,1024})for(boolean phone:new boolean[]{false,true}){condition(root,type,block,phone);++cases;}
        }
        failure(root,NativeMmfRenderer.Failure.CREATE,false,false);
        failure(root,NativeMmfRenderer.Failure.RENDER,false,false);
        failure(root,NativeMmfRenderer.Failure.NONE,true,false);
        failure(root,NativeMmfRenderer.Failure.NONE,false,true);
        check(cases==(legacyOnly?30:60),"expected case coverage");
        System.out.println("Progressive render PASS: "+cases+" synthetic PCM cases plus4 failure/cancel/move-cleanup cases; actual Java renderLoop, no Android audio/JNI/device.");
    }
}
