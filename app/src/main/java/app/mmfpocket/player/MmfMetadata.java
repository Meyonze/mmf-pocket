package app.mmfpocket.player;

import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;

/** Bounded, display-only SMAF metadata reader. Never scans score/wave payloads. */
final class MmfMetadata {
    static final MmfMetadata EMPTY = new MmfMetadata();
    private static final Charset SHIFT_JIS = Charset.forName("windows-31j");
    private static final int MAX_CHUNKS = 4096;
    private static final int MAX_FIELDS = 2048;
    private static final int MAX_TEXT_BYTES = 65536;
    private static final int MAX_DISPLAY_CHARS = 256;

    private String title = "", artist = "", artistAlias = "";
    private String composer = "", lyricist = "", arranger = "";

    private MmfMetadata() {}

    String title() { return title; }
    String artist() { return artist.isEmpty() ? artistAlias : artist; }
    String composer() { return composer; }
    String lyricist() { return lyricist; }
    String arranger() { return arranger; }

    static MmfMetadata read(byte[] data) {
        if (data == null || data.length < 8 || !tag(data, 0, "MMMD")) return EMPTY;
        int end = (int) Math.min(data.length, 8L + u32(data, 4));
        MmfMetadata contents = new MmfMetadata(), optional = new MmfMetadata();
        for (int p = 8, chunks = 0; p <= end - 8 && chunks++ < MAX_CHUNKS;) {
            long size = u32(data, p + 4);
            int body = p + 8;
            if (size > end - body) break;
            int next = body + (int) size;
            if (tag(data, p, "CNTI") && size >= 5) {
                contents.readContents(data, body + 5, next, data[body + 2] & 255);
            } else if (tag(data, p, "OPDA")) {
                optional.readOptional(data, body, next);
            }
            p = next;
        }
        contents.title = prefer(optional.title, contents.title);
        contents.artist = prefer(optional.artist(), contents.artist());
        contents.composer = prefer(optional.composer, contents.composer);
        contents.lyricist = prefer(optional.lyricist, contents.lyricist);
        contents.arranger = prefer(optional.arranger, contents.arranger);
        return contents;
    }

    private void readContents(byte[] data, int start, int end, int code) {
        if (end - start > MAX_TEXT_BYTES) return;
        // Decode first: a Shift_JIS trail byte may be ASCII backslash/comma.
        String text = decode(data, start, end - start, code);
        StringBuilder field = new StringBuilder();
        boolean escaped = false;
        int fields = 0;
        for (int i = 0; i < text.length() && fields < MAX_FIELDS; i++) {
            char c = text.charAt(i);
            if (c == '\0') {
                contentsField(field.toString());
                return;
            } else if (escaped) {
                field.append(c);
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == ',') {
                contentsField(field.toString());
                field.setLength(0);
                fields++;
            } else {
                field.append(c);
            }
        }
        if (escaped) field.append('\\');
        contentsField(field.toString());
    }

    private void contentsField(String field) {
        if (field.length() >= 3 && field.charAt(2) == ':') {
            put(field.substring(0, 2), field.substring(3));
        }
    }

    private void readOptional(byte[] data, int start, int end) {
        int fields = 0;
        for (int p = start, chunks = 0; p <= end - 8 && chunks++ < MAX_CHUNKS;) {
            long size = u32(data, p + 4);
            int body = p + 8;
            if (size > end - body) break;
            int next = body + (int) size;
            if (data[p] == 'D' && data[p + 1] == 'c' && data[p + 2] == 'h') {
                // Dch's final ID byte declares its own charset, not CNTI's.
                int code = data[p + 3] & 255;
                for (int q = body; q <= next - 4 && fields++ < MAX_FIELDS;) {
                    int length = ((data[q + 2] & 255) << 8) | (data[q + 3] & 255);
                    int value = q + 4;
                    if (length > next - value) break;
                    String key = new String(data, q, 2, StandardCharsets.US_ASCII);
                    if (known(key)) put(key, decode(data, value, length, code));
                    q = value + length;
                }
            }
            p = next;
        }
    }

    private void put(String key, String value) {
        value = clean(value);
        if (value.isEmpty()) return;
        switch (key) {
            case "ST": if (title.isEmpty()) title = value; break;
            case "AN": if (artist.isEmpty()) artist = value; break;
            case "AR": if (artistAlias.isEmpty()) artistAlias = value; break;
            case "SW": if (composer.isEmpty()) composer = value; break;
            case "WW": if (lyricist.isEmpty()) lyricist = value; break;
            case "AW": if (arranger.isEmpty()) arranger = value; break;
            default: break;
        }
    }

    private static boolean known(String key) {
        return key.equals("ST") || key.equals("AN") || key.equals("AR")
                || key.equals("SW") || key.equals("WW") || key.equals("AW");
    }

    private static String decode(byte[] data, int start, int length, int code) {
        if (code == 0) return new String(data, start, length, SHIFT_JIS);
        // Unknown code types: preserve unambiguous ASCII, avoid charset guesses.
        for (int i = start; i < start + length; i++) if (data[i] < 0) return "";
        return new String(data, start, length, StandardCharsets.US_ASCII);
    }

    private static String clean(String value) {
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < value.length() && out.length() < MAX_DISPLAY_CHARS;) {
            int c = value.codePointAt(i);
            i += Character.charCount(c);
            if (c == 0) break;
            if (Character.isISOControl(c) || Character.isWhitespace(c)) c = ' ';
            if (c != 0xfffd && Character.getType(c) != Character.FORMAT) out.appendCodePoint(c);
        }
        return out.toString().trim();
    }

    private static String prefer(String primary, String fallback) {
        return primary.isEmpty() ? fallback : primary;
    }

    private static boolean tag(byte[] data, int offset, String value) {
        for (int i = 0; i < 4; i++) if (data[offset + i] != value.charAt(i)) return false;
        return true;
    }

    private static long u32(byte[] data, int offset) {
        return ((data[offset] & 255L) << 24) | ((data[offset + 1] & 255L) << 16)
                | ((data[offset + 2] & 255L) << 8) | (data[offset + 3] & 255L);
    }
}
