// SPDX-License-Identifier: Apache-2.0
// Deterministic host I/O double, NOT an Android AudioTrack implementation.
package android.media;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.io.ByteArrayOutputStream;
import java.util.function.Consumer;
public final class AudioTrack {
    public static final int MODE_STREAM=1, STATE_INITIALIZED=1, WRITE_NON_BLOCKING=1;
    public static final List<AudioTrack> tracks=new CopyOnWriteArrayList<>();
    public static Consumer<Integer> onBuild=id->{};
    public static Consumer<AudioTrack> onWrite=track->{};
    public static Consumer<AudioTrack> onHead=track->{};
    public static int maxWriteBytes=Integer.MAX_VALUE;
    public static boolean autoDrain=true;
    public final int id;
    public final ByteArrayOutputStream written=new ByteArrayOutputStream();
    public volatile int head, releases, writes;
    public volatile boolean playing;
    private AudioTrack(int id){this.id=id;}
    public static void reset(){tracks.clear();onBuild=id->{};onWrite=track->{};onHead=track->{};maxWriteBytes=Integer.MAX_VALUE;autoDrain=true;}
    public static int getMinBufferSize(int rate,int channels,int encoding){return 4096;}
    public int getState(){return STATE_INITIALIZED;}
    public int getPlaybackHeadPosition(){onHead.accept(this);return head;}
    public int setVolume(float value){return 0;}
    public int write(byte[] data,int offset,int size,int mode){
        ++writes;int count=Math.min(size,maxWriteBytes);written.write(data,offset,count);
        if(autoDrain)head+=count/4;
        onWrite.accept(this);return count;
    }
    public void pause(){playing=false;}
    public void play(){playing=true;}
    public void flush(){}
    public void stop(){playing=false;}
    public void release(){++releases;}
    public static final class Builder {
        public Builder setAudioAttributes(AudioAttributes value){return this;}
        public Builder setAudioFormat(AudioFormat value){return this;}
        public Builder setTransferMode(int value){return this;}
        public Builder setBufferSizeInBytes(int value){return this;}
        public AudioTrack build(){
            int id=tracks.size()+1;onBuild.accept(id);
            AudioTrack track=new AudioTrack(id);tracks.add(track);return track;
        }
    }
}
