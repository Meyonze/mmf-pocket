package app.mmfpocket.player;

import java.util.Arrays;

/** List row names: the file name, or the title embedded in the MMF. */
final class ListNames {
    /**
     * The embedded title lives in the leading CNTI/OPDA chunks, ahead of the
     * (large) score and wave data. Reading only this much keeps listing a big
     * folder cheap; a file whose title lies beyond it falls back to its name.
     */
    static final int MAX_PREFIX_BYTES = 64 * 1024;

    private ListNames() {}

    /** The display title from the first {@code length} bytes of a file, or "". */
    static String embeddedTitle(byte[] prefix, int length) {
        if (prefix == null || length < 8) return "";
        byte[] data = length == prefix.length ? prefix : Arrays.copyOf(prefix, length);
        MmfMetadata metadata = MmfMetadata.read(data);
        // Same separation of credits as the player heading, so "Song by Artist"
        // shows just the song in both places.
        return SongDisplayText.from(metadata.title(), metadata.artist(), metadata.composer(),
                metadata.lyricist(), metadata.arranger()).title;
    }

    /** The row text; the file name whenever the title is unavailable or not wanted. */
    static String display(String fileName, String embeddedTitle, boolean preferTitle) {
        if (preferTitle && embeddedTitle != null && !embeddedTitle.trim().isEmpty()) {
            return embeddedTitle;
        }
        return fileName;
    }
}
