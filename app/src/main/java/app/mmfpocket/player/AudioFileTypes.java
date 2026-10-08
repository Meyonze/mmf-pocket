package app.mmfpocket.player;

import java.util.Locale;

/** Filename policy shared by folder discovery, playback and batch conversion. */
final class AudioFileTypes {
    static final String MIME_MMF = "audio/x-smaf";
    static final String MIME_WAV = "audio/wav";

    private AudioFileTypes() {}

    static boolean isMmf(String name) {
        return name != null && name.toLowerCase(Locale.ROOT).endsWith(".mmf");
    }

    static boolean isWav(String name) {
        return name != null && name.toLowerCase(Locale.ROOT).endsWith(".wav");
    }

    static boolean isPlayable(String name) {
        return isMmf(name) || isWav(name);
    }

    static String mimeType(String name) {
        return isWav(name) ? MIME_WAV : MIME_MMF;
    }

    static boolean isWavMedia(String mime, CharSequence title) {
        if (title != null && isMmf(title.toString())) return false;
        return title != null && isWav(title.toString())
                || MIME_WAV.equalsIgnoreCase(mime)
                || "audio/x-wav".equalsIgnoreCase(mime)
                || "audio/wave".equalsIgnoreCase(mime)
                || "audio/vnd.wave".equalsIgnoreCase(mime);
    }
}
