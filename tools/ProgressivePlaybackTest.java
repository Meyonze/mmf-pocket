// SPDX-License-Identifier: Apache-2.0
// Runs actual production playback control code using deterministic host doubles.
// No device, real-time latency, native decoding or DSP fidelity is tested.
package app.mmfpocket.player;
import android.media.AudioTrack;
import java.io.File;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;

public final class ProgressivePlaybackTest {
    static final class Events implements ProgressivePlayback.Listener {
        volatile int ready, completed, errors;
        Runnable onReady=()->{};
        public void onReady(){++ready;onReady.run();}
        public void onCacheReady(File cached){throw new AssertionError("render forbidden");}
        public void onCompleted(){++completed;}
        public void onError(String message){++errors;}
    }
    static void check(boolean ok,String message){if(!ok)throw new AssertionError(message);}
    static void set(Object player,String name,Object value)throws Exception {
        Field field=ProgressivePlayback.class.getDeclaredField(name);field.setAccessible(true);field.set(player,value);
    }
    static Object get(Object player,String name)throws Exception {
        Field field=ProgressivePlayback.class.getDeclaredField(name);field.setAccessible(true);return field.get(player);
    }
    static void call(Object player,String name,Class<?>[] types,Object... args)throws Exception {
        Method method=ProgressivePlayback.class.getDeclaredMethod(name,types);method.setAccessible(true);method.invoke(player,args);
    }
    static void await(CountDownLatch latch){
        try {check(latch.await(3,TimeUnit.SECONDS),"barrier timed out");}
        catch(InterruptedException error){Thread.currentThread().interrupt();throw new AssertionError(error);}
    }
    static void until(BooleanSupplier condition,String message)throws Exception {
        long deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(3);
        while(!condition.getAsBoolean()&&System.nanoTime()<deadline)Thread.sleep(1);
        check(condition.getAsBoolean(),message);
    }
    static Thread worker(ProgressivePlayback player,AtomicReference<Throwable> failure,Runnable action) {
        Thread thread=new Thread(()->{try{action.run();}catch(Throwable error){failure.set(error);}},"host-playback-test");
        thread.setDaemon(true);thread.start();return thread;
    }
    static Runnable playback(ProgressivePlayback player){return ()->{try{call(player,"playbackLoop",new Class<?>[0]);}catch(Exception error){throw new AssertionError(error);}};}
    static void finish(ProgressivePlayback player,Thread thread,AtomicReference<Throwable> failure)throws Exception {
        try {thread.join(1500);check(!thread.isAlive(),"playback thread did not terminate");check(failure.get()==null,"host worker threw: "+failure.get());}
        finally {if(thread.isAlive()){player.cancel();thread.interrupt();thread.join(1000);}}
    }
    static byte[] pcm(int frames){byte[] bytes=new byte[frames*4];for(int i=0;i<frames;++i){bytes[i*4]=(byte)i;bytes[i*4+1]=(byte)(i>>>8);bytes[i*4+2]=(byte)~i;bytes[i*4+3]=(byte)(~i>>>8);}return bytes;}
    static ProgressivePlayback fixture(Path root,Events events,int frames,boolean complete)throws Exception {
        AudioTrack.reset();File file=root.resolve("synthetic.wav").toFile();byte[] data=new byte[44+frames*4];System.arraycopy(pcm(frames),0,data,44,frames*4);Files.write(file.toPath(),data);
        ProgressivePlayback player=new ProgressivePlayback(new byte[0],false,file,root.resolve("unused.wav").toFile(),events);
        set(player,"renderedFrames",(long)frames);set(player,"scoreEndFrames",(long)frames);set(player,"finalFrames",(long)frames);set(player,"renderingComplete",complete);return player;
    }
    static void pausedFailure(Path root)throws Exception {
        Events events=new Events();ProgressivePlayback player=fixture(root,events,24000,false);
        events.onReady=player::pause;AtomicReference<Throwable> failure=new AtomicReference<>();Thread thread=worker(player,failure,playback(player));
        try {until(()->thread.getState()==Thread.State.WAITING,"not waiting while paused");call(player,"fail",new Class<?>[]{String.class},"injected render failure");finish(player,thread,failure);
            check(events.errors==1&&events.completed==0&&!player.isReady(),"paused failure teardown");check(AudioTrack.tracks.get(0).releases==1,"paused failure release");}
        finally {player.cancel();thread.interrupt();thread.join(1000);}
        System.out.println("PASS paused playback exits after producer failure; track released once");
    }
    static void drainFailure(Path root)throws Exception {
        Events events=new Events();ProgressivePlayback player=fixture(root,events,64,true);player.pause();
        AtomicReference<Throwable> failure=new AtomicReference<>();Thread thread=worker(player,failure,()->{try{call(player,"waitForDrain",new Class<?>[]{AudioTrack.class,long.class},new AudioTrack.Builder().build(),64L);}catch(Exception error){throw new AssertionError(error);}});
        try {until(()->thread.getState()==Thread.State.WAITING,"drain not waiting while paused");call(player,"fail",new Class<?>[]{String.class},"injected drain failure");finish(player,thread,failure);check(events.errors==1&&events.completed==0,"drain failure must not complete");}
        finally {player.cancel();thread.interrupt();thread.join(1000);}
        System.out.println("PASS paused drain exits after producer failure");
    }
    static void blockedWriteFailure(Path root)throws Exception {
        Events events=new Events();ProgressivePlayback player=fixture(root,events,512,true);
        AudioTrack.maxWriteBytes=0;CountDownLatch writing=new CountDownLatch(1);AudioTrack.onWrite=track->writing.countDown();
        AtomicReference<Throwable> failure=new AtomicReference<>();Thread thread=worker(player,failure,playback(player));
        try {await(writing);call(player,"fail",new Class<?>[]{String.class},"injected write-wait failure");finish(player,thread,failure);
            check(events.errors==1&&events.completed==0&&AudioTrack.tracks.get(0).releases==1,"blocked write failure teardown");}
        finally {player.cancel();thread.interrupt();thread.join(1000);}
        System.out.println("PASS zero-byte nonblocking write exits after producer failure");
    }
    static void drainSeek(Path root)throws Exception {
        Events events=new Events();ProgressivePlayback player=fixture(root,events,512,true);AudioTrack track=new AudioTrack.Builder().build();track.head=512;
        CountDownLatch querying=new CountDownLatch(1),proceed=new CountDownLatch(1);AudioTrack.onHead=t->{querying.countDown();await(proceed);};
        AtomicReference<Throwable> failure=new AtomicReference<>();AtomicReference<Boolean> drained=new AtomicReference<>();
        Thread thread=worker(player,failure,()->{try{Method method=ProgressivePlayback.class.getDeclaredMethod("waitForDrain",AudioTrack.class,long.class);method.setAccessible(true);drained.set((Boolean)method.invoke(player,track,512L));}catch(Exception error){throw new AssertionError(error);}});
        try {await(querying);player.seekTo(300);proceed.countDown();finish(player,thread,failure);
            check(Boolean.FALSE.equals(drained.get()),"seek during head query must veto drain completion");
            check(player.getPositionFrames()==300,"drain must not overwrite pending seek position");}
        finally {proceed.countDown();player.cancel();thread.interrupt();thread.join(1000);}
        System.out.println("PASS seek during drain head query preserves requested position and vetoes completion");
    }
    static void seeks(Path root,long second)throws Exception {
        Events events=new Events();ProgressivePlayback player=fixture(root,events,512,true);events.onReady=()->player.seekTo(100);
        CountDownLatch building=new CountDownLatch(1),proceed=new CountDownLatch(1);
        AudioTrack.onBuild=id->{if(id==2){building.countDown();await(proceed);}};
        AtomicReference<Throwable> failure=new AtomicReference<>();Thread thread=worker(player,failure,playback(player));
        try {await(building);check(player.seekTo(second)==second,"second seek accepted");check(player.getPositionFrames()==second,"pending position must reflect newest seek");proceed.countDown();finish(player,thread,failure);
            check(events.errors==0&&events.completed==1,"seek playback completes once");
            check(AudioTrack.tracks.size()==3,"new seek must survive slow track recreation, including identical destinations");
            check(AudioTrack.tracks.get(1).written.size()==0,"superseded seek must not submit stale PCM");
            byte[] expected=Arrays.copyOfRange(pcm(512),(int)second*4,512*4);
            check(Arrays.equals(expected,AudioTrack.tracks.get(2).written.toByteArray()),"latest seek PCM exact");
            for(AudioTrack track:AudioTrack.tracks)check(track.releases==1,"seek track release once");}
        finally {proceed.countDown();player.cancel();thread.interrupt();thread.join(1000);}
        System.out.println("PASS latest seek retained during track recreation: "+second);
    }
    static void replacementFailure(Path root)throws Exception {
        Events events=new Events();ProgressivePlayback player=fixture(root,events,512,true);events.onReady=()->player.seekTo(100);
        AudioTrack.onBuild=id->{if(id==2)throw new IllegalStateException("injected replacement failure");};
        AtomicReference<Throwable> failure=new AtomicReference<>();Thread thread=worker(player,failure,playback(player));
        try {finish(player,thread,failure);check(events.errors==1&&events.completed==0&&!player.isReady(),"replacement failure teardown");
            check(AudioTrack.tracks.size()==1&&AudioTrack.tracks.get(0).releases==1,"failed replacement must not release old track twice");}
        finally {player.cancel();thread.interrupt();thread.join(1000);}
        System.out.println("PASS failed replacement releases original track exactly once");
    }
    static void ordinary(Path root,int chunk,boolean pause,boolean seekWhilePaused)throws Exception {
        Events events=new Events();ProgressivePlayback player=fixture(root,events,2301,true);AudioTrack.maxWriteBytes=chunk*4;
        CountDownLatch interrupted=new CountDownLatch(1);boolean[] once={false};
        if(pause)AudioTrack.onWrite=track->{if(!once[0]){once[0]=true;player.pause();interrupted.countDown();}};
        AtomicReference<Throwable> failure=new AtomicReference<>();Thread thread=worker(player,failure,playback(player));
        long target=0;
        try {if(pause){await(interrupted);until(()->thread.getState()==Thread.State.WAITING,"pause not observed");if(seekWhilePaused){target=1300;player.seekTo(target);until(()->AudioTrack.tracks.size()==2,"paused seek not applied");check(player.getPositionFrames()==target,"paused seek position");}player.resume();}
            finish(player,thread,failure);check(events.ready==1&&events.completed==1&&events.errors==0,"ordinary callbacks");
            byte[] expected=seekWhilePaused?Arrays.copyOfRange(pcm(2301),(int)target*4,2301*4):pcm(2301);
            AudioTrack finalTrack=AudioTrack.tracks.get(AudioTrack.tracks.size()-1);
            check(Arrays.equals(expected,finalTrack.written.toByteArray()),"unaltered PCM across partial write/pause/seek");
            check(player.getPositionFrames()==2301,"end position");for(AudioTrack track:AudioTrack.tracks)check(track.releases==1,"ordinary release once");}
        finally {player.cancel();thread.interrupt();thread.join(1000);}
    }
    public static void main(String[] args)throws Exception {
        Path root=Path.of(args[0]);Files.createDirectories(root);String mode=args.length>1?args[1]:"all";
        if(mode.equals("all")||mode.equals("paused"))pausedFailure(root);
        if(mode.equals("all")||mode.equals("drain"))drainFailure(root);
        if(mode.equals("all")||mode.equals("write"))blockedWriteFailure(root);
        if(mode.equals("all")||mode.equals("drain-seek"))drainSeek(root);
        if(mode.equals("all")||mode.equals("replacement-failure"))replacementFailure(root);
        if(mode.equals("all")||mode.equals("seek")){seeks(root,300);seeks(root,100);seeks(root,0);}
        if(mode.equals("all")||mode.equals("ordinary")){for(int chunk:new int[]{1,31,1024}){ordinary(root,chunk,false,false);ordinary(root,chunk,true,false);ordinary(root,chunk,true,true);}System.out.println("PASS 9 ordinary whole-PCM, partial-write, pause/resume and paused-seek cases");}
        System.out.println("PASS actual Java control flow with deterministic host doubles only; no Android device/JNI/DSP");
    }
}
