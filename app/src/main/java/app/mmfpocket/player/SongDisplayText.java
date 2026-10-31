package app.mmfpocket.player;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** Display-only separation of title-embedded credits; leaves file tags intact. */
final class SongDisplayText {
    final String title;
    final String byline;

    private SongDisplayText(String title, String byline) {
        this.title = title;
        this.byline = byline;
    }

    static SongDisplayText from(CharSequence heading, CharSequence... credits) {
        String title = heading == null ? "" : trimSpace(heading.toString());
        List<String> names = new ArrayList<>();
        for (CharSequence credit : credits) {
            if (credit != null) add(names, credit.toString());
        }
        // A standalone "by" is a credit marker only with supporting evidence.
        // In particular, do not turn an ordinary title like "Stand by Me"
        // into a title/author pair merely because it contains the word "by".
        for (int at = title.length() - 3; at >= 1; at--) {
            if ((title.charAt(at) != 'b' && title.charAt(at) != 'B')
                    || (title.charAt(at + 1) != 'y' && title.charAt(at + 1) != 'Y')
                    || !space(title.charAt(at - 1))
                    || !space(title.charAt(at + 2))) continue;
            String song = trimSpace(title.substring(0, at));
            String embedded = trimSpace(title.substring(at + 2));
            if (song.isEmpty() || embedded.isEmpty() || !creditLike(embedded, names)) continue;
            List<String> combined = new ArrayList<>();
            // Split spaced separators, preserving URL path/scheme slashes.
            for (String part : embedded.split("\\s+/\\s+")) add(combined, part);
            for (String name : names) add(combined, name);
            names = combined;
            title = song;
            break;
        }
        return new SongDisplayText(title, names.isEmpty() ? "" : "by " + String.join(" / ", names));
    }

    private static boolean creditLike(String text, List<String> names) {
        if (names.contains(text) || text.matches(".*\\s+/\\s+.*")) return true;
        String lower = text.toLowerCase(Locale.ROOT);
        if (lower.contains("https://") || lower.contains("http://") || lower.contains("www.")
                || lower.matches(".*\\.(com|net|org|jp)(?:\\b|/).*$")) return true;
        for (int i = 0; i < text.length();) {
            int c = text.codePointAt(i);
            i += Character.charCount(c);
            Character.UnicodeScript script = Character.UnicodeScript.of(c);
            if (script == Character.UnicodeScript.HAN || script == Character.UnicodeScript.HIRAGANA
                    || script == Character.UnicodeScript.KATAKANA) return true;
        }
        return false;
    }

    private static boolean space(char c) {
        return Character.isWhitespace(c) || Character.isSpaceChar(c);
    }

    private static String trimSpace(String text) {
        int start = 0, end = text.length();
        while (start < end && space(text.charAt(start))) start++;
        while (end > start && space(text.charAt(end - 1))) end--;
        return text.substring(start, end);
    }

    private static void add(List<String> values, String value) {
        value = trimSpace(value);
        if (!value.isEmpty() && !values.contains(value)) values.add(value);
    }
}
