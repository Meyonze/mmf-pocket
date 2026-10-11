// SPDX-License-Identifier: Apache-2.0
// Compile-only host stub. No Android audio device is modeled or exercised.
package android.media;
public final class AudioAttributes {
    public static final int USAGE_MEDIA=1, CONTENT_TYPE_MUSIC=2;
    public static final class Builder {
        public Builder setUsage(int value){throw unavailable();}
        public Builder setContentType(int value){throw unavailable();}
        public AudioAttributes build(){throw unavailable();}
    }
    static UnsupportedOperationException unavailable(){return new UnsupportedOperationException("Audio device calls forbidden in render-only host test");}
}
