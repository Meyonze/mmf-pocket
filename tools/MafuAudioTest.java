package app.mmfpocket.player;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicReference;

/** Original synthetic visual measurements; no handset, microphone or playback DSP simulation. */
public final class MafuAudioTest {
    static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    static short[] sine(int frames, int rate, double frequency, boolean opposite) {
        short[] pcm = new short[frames * 2];
        for (int i = 0; i < frames; i++) {
            short value = (short)(Math.sin(2 * Math.PI * frequency * i / rate) * 16000);
            pcm[i*2] = value; pcm[i*2+1] = (short)(opposite ? -value : value);
        }
        return pcm;
    }
    static MafuAudioEnvelope envelope(short[] pcm, int chunk) {
        MafuAudioEnvelope result = new MafuAudioEnvelope();
        MafuAudioEnvelope.Builder builder = new MafuAudioEnvelope.Builder(result, 44100);
        for (int offset = 0; offset < pcm.length / 2; offset += chunk) {
            int count = Math.min(chunk, pcm.length / 2 - offset);
            builder.acceptStereo16(Arrays.copyOfRange(pcm, offset*2, (offset+count)*2), count);
        }
        builder.finish(); return result;
    }
    static void stereoAndBands() {
        short[] low = sine(44100,44100,80,false), anti = sine(44100,44100,80,true);
        short[] high = sine(44100,44100,6000,false), saved = low.clone();
        MafuAudioEnvelope a = envelope(low,1), b = envelope(anti,31), c = envelope(high,1024);
        float[] left = new float[4], right = new float[4], upper = new float[4];
        check(a.sample(400,left)&&b.sample(400,right)&&c.sample(400,upper),"band windows published");
        check(Arrays.equals(left,right),"opposite-phase stereo energy must not cancel");
        check(left[1] > upper[1]*2 && upper[2] > left[2]*2,"low/high frequency separation");
        check(Arrays.equals(low,saved),"analysis never mutates PCM");
        for (int chunk : new int[]{31,1024}) {
            MafuAudioEnvelope d = envelope(low,chunk);
            for (int ms=0;ms<1000;ms+=40) {
                a.sample(ms,left);d.sample(ms,right);check(Arrays.equals(left,right),"chunk-invariant measurement");
            }
        }
        check(!a.sample(-1,left)&&!a.sample(Long.MAX_VALUE,left)&&!a.sample(1000,left),"absolute availability bounds");
        System.out.println("PASS stereo anti-phase, bands, PCM immutability, block1/31/1024 and bounds");
    }
    static void silenceAndOnsets() {
        short[] pcm = new short[44100*2];
        short[] tone = sine(44100/2,44100,80,false);
        System.arraycopy(tone,0,pcm,44100, tone.length);
        MafuAudioEnvelope result = envelope(pcm,31);float[] sample=new float[4];
        check(result.sample(200,sample)&&sample[0]==0&&sample[1]==0&&sample[2]==0&&sample[3]==0,"measured silence distinct from unavailable");
        // Sound begins at500ms, inside the480..520ms measurement window.
        check(result.sample(480,sample)&&sample[0]>.3&&sample[3]>.2,"signal onset after leading silence");
        MafuAudioEnvelope invalid = new MafuAudioEnvelope();
        new MafuAudioEnvelope.Builder(invalid,44100).add(Double.NaN,0);
        check(!invalid.sample(0,sample),"nonfinite visual input rejected");
        MafuAudioEnvelope oddRate = new MafuAudioEnvelope();
        MafuAudioEnvelope.Builder oddBuilder = new MafuAudioEnvelope.Builder(oddRate,12345);
        for(int i=0;i<12345*2;i++)oddBuilder.add(.2,-.2);
        oddBuilder.finish();check(oddRate.size()==50,"noninteger window lengths must not drift");
        System.out.println("PASS leading silence/onset and nonfinite visual rejection");
    }
    static byte[] wav(int bits, int format, boolean extra, boolean extensible) throws IOException {
        int rate=8000, frames=1600, channels=2, align=channels*bits/8;
        ByteArrayOutputStream body = new ByteArrayOutputStream();body.write("WAVE".getBytes(java.nio.charset.StandardCharsets.US_ASCII));
        if(extra){body.write("JUNK".getBytes(java.nio.charset.StandardCharsets.US_ASCII));body.write(le(3));body.write(new byte[]{1,2,3,0});}
        ByteBuffer fmt=ByteBuffer.allocate(extensible?40:16).order(ByteOrder.LITTLE_ENDIAN);
        fmt.putShort((short)(extensible?0xfffe:format)).putShort((short)channels).putInt(rate).putInt(rate*align).putShort((short)align).putShort((short)bits);
        if(extensible){fmt.putShort((short)22).putShort((short)bits).putInt(3).putInt(format).putShort((short)0).putShort((short)0x10).put(new byte[]{(byte)0x80,0,0,(byte)0xaa,0,0x38,(byte)0x9b,0x71});}
        body.write("fmt ".getBytes(java.nio.charset.StandardCharsets.US_ASCII));body.write(le(fmt.capacity()));body.write(fmt.array());
        body.write("data".getBytes(java.nio.charset.StandardCharsets.US_ASCII));body.write(le(frames*align));
        ByteBuffer pcm=ByteBuffer.allocate(frames*align).order(ByteOrder.LITTLE_ENDIAN);
        for(int frame=0;frame<frames;frame++)for(int ch=0;ch<channels;ch++) {
            double value=Math.sin(frame*2*Math.PI*80/rate)*.5;
            if(format==3)pcm.putFloat((float)value);
            else if(bits==8)pcm.put((byte)(128+(int)(value*127)));
            else if(bits==16)pcm.putShort((short)(value*32767));
            else if(bits==24){int raw=(int)(value*8388607);pcm.put((byte)raw).put((byte)(raw>>>8)).put((byte)(raw>>>16));}
            else pcm.putInt((int)(value*2147483647));
        }
        body.write(pcm.array());ByteArrayOutputStream output=new ByteArrayOutputStream();
        output.write("RIFF".getBytes(java.nio.charset.StandardCharsets.US_ASCII));output.write(le(body.size()));body.writeTo(output);return output.toByteArray();
    }
    static byte[] le(int value){return ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(value).array();}
    static void wavCases() throws Exception {
        float[] sample=new float[4];int cases=0;
        for(int bits:new int[]{8,16,24,32})for(boolean extra:new boolean[]{false,true})for(boolean ext:new boolean[]{false,true}) {
            MafuAudioEnvelope e=new MafuAudioEnvelope();
            check(MafuWavAnalysis.read(new ByteArrayInputStream(wav(bits,1,extra,ext)),e,()->false),"PCM WAV grammar");
            check(e.sample(80,sample)&&sample[0]>.5,"PCM WAV level");++cases;
        }
        for(boolean extra:new boolean[]{false,true})for(boolean ext:new boolean[]{false,true}) {
            MafuAudioEnvelope e=new MafuAudioEnvelope();
            check(MafuWavAnalysis.read(new ByteArrayInputStream(wav(32,3,extra,ext)),e,()->false)&&e.sample(80,sample)&&sample[0]>.5,"float WAV grammar");++cases;
        }
        for(int bits:new int[]{8,16,24,32}) {
            byte[] stereo=wav(bits,1,false,false);int width=bits/8;
            byte[] mono=new byte[44+1600*width];System.arraycopy(stereo,0,mono,0,44);
            ByteBuffer header=ByteBuffer.wrap(mono).order(ByteOrder.LITTLE_ENDIAN);
            header.putInt(4,mono.length-8).putShort(22,(short)1).putInt(28,8000*width).putShort(32,(short)width).putInt(40,1600*width);
            for(int i=0;i<1600;i++)System.arraycopy(stereo,44+i*2*width,mono,44+i*width,width);
            MafuAudioEnvelope e=new MafuAudioEnvelope();
            check(MafuWavAnalysis.read(new ByteArrayInputStream(mono),e,()->false)&&e.sample(80,sample)&&sample[0]>.5,"mono PCM WAV grammar");++cases;
        }
        byte[] data=wav(16,1,true,false), prefixed=new byte[data.length+31];System.arraycopy(data,0,prefixed,17,data.length);
        check(MafuWavAnalysis.read(new ByteArrayInputStream(prefixed,17,data.length),new MafuAudioEnvelope(),()->false),"bounded input subsection");
        byte[] unsupported=data.clone();unsupported[32]=6;unsupported[33]=0;
        check(!MafuWavAnalysis.read(new ByteArrayInputStream(unsupported),new MafuAudioEnvelope(),()->false),"unknown WAV codec unavailable");
        byte[] wrongBound=data.clone();System.arraycopy(le(4),0,wrongBound,4,4);
        check(!MafuWavAnalysis.read(new ByteArrayInputStream(wrongBound),new MafuAudioEnvelope(),()->false),"RIFF parent boundary");
        check(!MafuWavAnalysis.read(new ByteArrayInputStream(data),new MafuAudioEnvelope(),()->true),"cancellation");
        try{MafuWavAnalysis.read(new ByteArrayInputStream(Arrays.copyOf(data,data.length-1)),new MafuAudioEnvelope(),()->false);throw new AssertionError("truncation accepted");}catch(java.io.EOFException expected){}
        System.out.println("PASS "+cases+" PCM8/16/24/32+float/extensible/odd-chunk WAV cases, bounded subsection, codec/parent/truncation/cancel");
    }
    static void sourceOwnership() {
        MafuAudioVisualization.Source old=MafuAudioVisualization.start("same");
        MafuAudioVisualization.Source next=MafuAudioVisualization.start("same");
        MafuAudioVisualization.clear(old);
        check(MafuAudioVisualization.find("same",old.token)==null&&MafuAudioVisualization.find("same",next.token)==next,"old generation cannot clear newer identical media");
        check(MafuAudioVisualization.find("different",next.token)==null,"media identity guard");
        MafuAudioVisualization.clear(next);check(MafuAudioVisualization.find("same",next.token)==null,"stop clears source");
        System.out.println("PASS identical-media source generations, conditional clear and stop");
    }
    static void dance() {
        MafuAudioDance dance=new MafuAudioDance();float[] beat={.7f,.6f,.3f,1};
        dance.update(1,0,1f/60,beat,0);float first=dance.pulse;
        check(first>0&&first<1,"smooth beat attack rather than single-frame jump");
        for(int i=1;i<60;i++)dance.update(1,i*16,1f/60,new float[]{.7f,.6f,.3f,0},0);
        check(dance.pulse<.03,"onset must not retrigger held frame");
        for(int i=0;i<150;i++)dance.update(1,1000+i*16,1f/60,new float[4],0);
        check(dance.level<.001&&dance.pulse<.001,"silence settles");
        dance.update(2,0,0,new float[4],0);check(dance.level==0&&dance.action==-1,"new source resets motion");
        dance.update(2,5000,1f/60,beat,0);dance.update(2,0,0,new float[4],0);check(dance.level==0,"backward seek resets motion");
        boolean[] actions=new boolean[4];dance.reset();
        for(int frame=0;frame<60*45;frame++) {
            long position=frame*1000L/60;
            dance.update(3,position,1f/60,new float[]{.8f,.7f,.4f,position%400<40?1:0},0);
            if(dance.action>=0)actions[dance.action]=true;
        }
        for(boolean action:actions)check(action,"beat-driven cartwheel/backflip/pirouette/hop variety");
        dance.action=1;dance.actionSeconds=.4f;
        for(int i=0;i<210;i++)dance.update(3,45000+i*16,1f/60,new float[4],0);
        check(dance.action==-1&&dance.level<.001,"missing measurements settle without freezing a flourish");
        System.out.println("PASS smooth attack/release, silence, source and seek reset");
    }
    static void publicationAndCap() throws Exception {
        MafuAudioEnvelope e=new MafuAudioEnvelope();MafuAudioEnvelope.Builder b=new MafuAudioEnvelope.Builder(e,1000);
        AtomicReference<Throwable> failure=new AtomicReference<>();
        Thread producer=new Thread(()->{try{for(int i=0;i<(MafuAudioEnvelope.MAX_WINDOWS+1)*40;i++)b.add(.1,-.1);b.finish();}catch(Throwable error){failure.set(error);}});
        producer.start();float[] sample=new float[4];
        while(producer.isAlive()){int count=e.size();if(count>0&&e.sample((count-1)*40L,sample))for(float value:sample)check(Float.isFinite(value)&&value>=0&&value<=1,"safe complete-window publication");}
        producer.join();check(failure.get()==null&&e.size()==MafuAudioEnvelope.MAX_WINDOWS,"hard bounded window cap");
        check(!e.sample(MafuAudioEnvelope.MAX_WINDOWS*40L,sample),"cap does not wrap source time");
        System.out.println("PASS concurrent complete-window publication and bounded one-hour timeline");
    }
    public static void main(String[] args) throws Exception {
        stereoAndBands();silenceAndOnsets();wavCases();sourceOwnership();dance();publicationAndCap();
        System.out.println("Mafu audio visualization host tests PASS. No Android device/provider timing or actual sound-output test.");
    }
}
