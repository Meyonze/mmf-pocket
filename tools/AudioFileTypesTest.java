package app.mmfpocket.player;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** Standalone, dependency-free checks for AudioFileTypes. */
public final class AudioFileTypesTest {
    private AudioFileTypesTest() {}

    public static void main(String[] args) {
        check(AudioFileTypes.isMmf("song.mmf"), "MMF extension");
        check(AudioFileTypes.isMmf("夜の 曲.MMF"), "uppercase MMF with Japanese and spaces");
        check(AudioFileTypes.isWav("  日本語 の 曲.WaV"), "mixed-case WAV with spaces");
        check(AudioFileTypes.isMmf("song.wav.mmf"), "last extension selects MMF");
        check(!AudioFileTypes.isWav("song.wav.mmf"), "wav.mmf is not WAV");
        check(AudioFileTypes.isWav("song.mmf.wav"), "last extension selects WAV");
        check(!AudioFileTypes.isMmf("song.mmf.wav"), "mmf.wav is not MMF");

        check(!AudioFileTypes.isMmf(null), "null is not MMF");
        check(!AudioFileTypes.isWav(null), "null is not WAV");
        check(!AudioFileTypes.isPlayable(null), "null is not playable");
        check(!AudioFileTypes.isPlayable("song.mp3"), "unsupported extension");
        check(!AudioFileTypes.isPlayable("song.wav "), "trailing extension whitespace is not trimmed");

        check(AudioFileTypes.MIME_WAV.equals(AudioFileTypes.mimeType("歌.WAV")),
                "WAV MIME selection ignores extension case");
        check(AudioFileTypes.MIME_MMF.equals(AudioFileTypes.mimeType("song.MMF")),
                "MMF MIME selection");
        check(AudioFileTypes.MIME_MMF.equals(AudioFileTypes.mimeType("song.mp3")),
                "unsupported MIME fallback remains MMF");

        for (String alias : new String[]{"audio/wav", "audio/x-wav", "audio/wave",
                "audio/vnd.wave"}) {
            check(AudioFileTypes.isWavMedia(alias, null), "WAV MIME alias " + alias);
            check(AudioFileTypes.isWavMedia(alias.toUpperCase(Locale.ROOT), null),
                    "case-insensitive WAV MIME alias " + alias);
        }
        check(AudioFileTypes.isWavMedia("application/octet-stream", "曲.wav"),
                "WAV title identifies WAV media");
        check(!AudioFileTypes.isWavMedia("audio/wav", "曲.MMF"),
                "MMF title takes precedence over WAV MIME");
        check(!AudioFileTypes.isWavMedia("application/octet-stream", "曲.mp3"),
                "unsupported title and MIME");
        check(!AudioFileTypes.isWavMedia(null, null), "null media metadata");

        Locale originalLocale = Locale.getDefault();
        try {
            Locale.setDefault(Locale.forLanguageTag("tr-TR"));
            check(AudioFileTypes.isMmf("PIANO.MMF"), "MMF extension stable under Turkish locale");
            check(AudioFileTypes.isWav("PIANO.WAV"), "WAV extension stable under Turkish locale");
            check(AudioFileTypes.isWavMedia("AUDIO/X-WAV", "PIANO.WAV"),
                    "WAV MIME/title stable under Turkish locale");
        } finally {
            Locale.setDefault(originalLocale);
        }

        List<String> wavOnly = List.of("one.wav", "二曲.WAV");
        check(filter(wavOnly, true).isEmpty(), "WAV-only folder has no MMF batch inputs");

        List<String> mixed = List.of("one.mmf", "two.WAV", "three.txt", "四曲.MMF");
        check(filter(mixed, true).equals(List.of("one.mmf", "四曲.MMF")),
                "batch classification selects only MMF files");
        check(filter(mixed, false).equals(List.of("one.mmf", "two.WAV", "四曲.MMF")),
                "mixed playback queue contains MMF and WAV only");

        System.out.println("AudioFileTypesTest: all checks passed");
    }

    private static List<String> filter(List<String> names, boolean mmfBatch) {
        List<String> selected = new ArrayList<>();
        for (String name : names) {
            if (mmfBatch ? AudioFileTypes.isMmf(name) : AudioFileTypes.isPlayable(name)) {
                selected.add(name);
            }
        }
        return selected;
    }

    private static void check(boolean condition, String description) {
        if (!condition) throw new AssertionError(description);
    }
}
