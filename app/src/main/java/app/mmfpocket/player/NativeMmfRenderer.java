package app.mmfpocket.player;

final class NativeMmfRenderer {
    static {
        System.loadLibrary("mmfplayer");
    }

    private NativeMmfRenderer() {}

    /** Returns the SMAF generation and score format used by the file. */
    static native String detectFormat(byte[] mmfData);

    /** Returns an empty string on success, otherwise a user-readable error. */
    static native String renderToWav(byte[] mmfData, String outputPath, boolean phoneSpeakerMode);

    /**
     * Creates an incremental renderer. On success sessionInfo receives the
     * native handle, score-end frame and hard upper-bound frame.
     */
    static native String createSession(
            byte[] mmfData, boolean phoneSpeakerMode, long[] sessionInfo);

    /** Returns rendered stereo frames, zero at the natural end, or -1 on error. */
    static native int renderSession(long handle, short[] stereoPcm);

    static native void destroySession(long handle);
}
