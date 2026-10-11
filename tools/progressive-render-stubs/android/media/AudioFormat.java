// SPDX-License-Identifier: Apache-2.0
// Compile-only host stub; never shipped with the Android app.
package android.media;
public final class AudioFormat {
    public static final int CHANNEL_OUT_STEREO=12, ENCODING_PCM_16BIT=2;
    public static final class Builder {
        public Builder setEncoding(int value){throw AudioAttributes.unavailable();}
        public Builder setSampleRate(int value){throw AudioAttributes.unavailable();}
        public Builder setChannelMask(int value){throw AudioAttributes.unavailable();}
        public AudioFormat build(){throw AudioAttributes.unavailable();}
    }
}
