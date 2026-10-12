package app.mmfpocket.player;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;

/** Actual renderLoop enabled/disabled observer bytes, using existing native PCM double. */
public final class MafuPcmObserverTest {
    static void check(boolean value,String text){if(!value)throw new AssertionError(text);}
    static void condition(Path root,int block,int fault) throws Exception {
        short[] pcm=new short[44100*2+74];
        for(int i=0;i<pcm.length/2;i++){short value=(short)(Math.sin(i*2*Math.PI*80/44100)*16000);pcm[i*2]=value;pcm[i*2+1]=(short)-value;}
        Path folder=Files.createTempDirectory(root,"tap-");
        Path wav=folder.resolve("cache.wav");
        ProgressiveRenderTest.Events events=new ProgressiveRenderTest.Events();
        NativeMmfRenderer.configure(pcm,block,NativeMmfRenderer.Failure.NONE);
        MafuAudioEnvelope envelope=new MafuAudioEnvelope();
        MafuAudioEnvelope.Builder builder=new MafuAudioEnvelope.Builder(envelope,44100);
        int[] calls={0,0};
        ProgressivePlayback.PcmObserver observer=new ProgressivePlayback.PcmObserver(){
            public void onPcm(short[] data,int frames){
                ++calls[0];if(fault==1)throw new IllegalStateException("injected visual failure");
                short[] before=data.clone();builder.acceptStereo16(data,frames);check(Arrays.equals(before,data),"tap must not mutate reused PCM");
            }
            public void onEnd(){++calls[1];if(fault==2)throw new IllegalStateException("injected end failure");builder.finish();}
        };
        ProgressivePlayback playback=fault==3?new ProgressivePlayback(new byte[]{1},false,folder.resolve("render.part").toFile(),wav.toFile(),events)
                :new ProgressivePlayback(new byte[]{1},false,folder.resolve("render.part").toFile(),wav.toFile(),events,observer);
        ProgressiveRenderTest.runRender(playback);
        check(events.errors==0&&events.cache==1&&playback.getRenderedFrames()==pcm.length/2,"visual failures do not fail/alter audio");
        check(Arrays.equals(Files.readAllBytes(wav),ProgressiveRenderTest.oracle(pcm)),"complete WAV/frames identical observer on/off/error");
        if(fault==1)check(calls[0]==1&&calls[1]==0,"failed observer permanently disabled");
        if(fault==0){float[] values=new float[4];check(calls[1]==1&&envelope.sample(400,values)&&values[0]>.5,"real envelope observer published sound");}
    }
    public static void main(String[] args) throws Exception {
        Path root=Path.of(args[0]);Files.createDirectories(root);
        for(int block:new int[]{1,31,1024})for(int fault=0;fault<4;fault++)condition(root,block,fault);
        System.out.println("PASS12 actual Java render cases: observer/envelope enabled, callback/end exceptions, no observer; complete WAV/frames PCM oracle exact; no JNI/Android audio.");
    }
}
