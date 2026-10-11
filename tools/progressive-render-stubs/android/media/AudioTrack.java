// SPDX-License-Identifier: Apache-2.0
// Compile-only host stub: every audio-device operation throws deliberately.
package android.media;
public final class AudioTrack {
    public static final int MODE_STREAM=1, STATE_INITIALIZED=1, WRITE_NON_BLOCKING=1;
    public static int getMinBufferSize(int rate,int channels,int encoding){throw AudioAttributes.unavailable();}
    public int getState(){throw AudioAttributes.unavailable();}
    public int getPlaybackHeadPosition(){throw AudioAttributes.unavailable();}
    public int setVolume(float value){throw AudioAttributes.unavailable();}
    public int write(byte[] data,int offset,int size,int mode){throw AudioAttributes.unavailable();}
    public void pause(){throw AudioAttributes.unavailable();}
    public void play(){throw AudioAttributes.unavailable();}
    public void flush(){throw AudioAttributes.unavailable();}
    public void stop(){throw AudioAttributes.unavailable();}
    public void release(){throw AudioAttributes.unavailable();}
    public static final class Builder {
        public Builder setAudioAttributes(AudioAttributes value){throw AudioAttributes.unavailable();}
        public Builder setAudioFormat(AudioFormat value){throw AudioAttributes.unavailable();}
        public Builder setTransferMode(int value){throw AudioAttributes.unavailable();}
        public Builder setBufferSizeInBytes(int value){throw AudioAttributes.unavailable();}
        public AudioTrack build(){throw AudioAttributes.unavailable();}
    }
}
