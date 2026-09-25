package app.mmfpocket.player;

final class NativeMmfRenderer {
    static {
        System.loadLibrary("mmfplayer");
    }

    private NativeMmfRenderer() {}

    /** Returns an empty string on success, otherwise a user-readable error. */
    static native String renderToWav(byte[] mmfData, String outputPath, boolean phoneSpeakerMode);
}
