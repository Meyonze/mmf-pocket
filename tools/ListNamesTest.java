package app.mmfpocket.player;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/** Original byte fixtures for the list-name choice; no private MMF files. */
public final class ListNamesTest {
    private static int checks;
    private static void equal(String expected, String actual) {
        checks++;
        if (!expected.equals(actual)) throw new AssertionError("Expected [" + expected + "] got [" + actual + "]");
    }
    private static byte[] ascii(String s) { return s.getBytes(StandardCharsets.US_ASCII); }
    private static byte[] concat(byte[]... arrays) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (byte[] a : arrays) out.writeBytes(a);
        return out.toByteArray();
    }
    private static byte[] u32(int n) { return new byte[]{(byte)(n>>24),(byte)(n>>16),(byte)(n>>8),(byte)n}; }
    private static byte[] chunk(String tag, byte[] body) { return concat(ascii(tag), u32(body.length), body); }
    private static byte[] file(byte[]... chunks) {
        byte[] body = concat(chunks);
        return concat(ascii("MMMD"), u32(body.length + 2), body, new byte[2]);
    }
    private static byte[] cnti(String text) { return chunk("CNTI", concat(new byte[]{0, 0, 0, 0, 0}, ascii(text))); }

    private static void display() {
        equal("a.mmf", ListNames.display("a.mmf", "Title", false));
        equal("Title", ListNames.display("a.mmf", "Title", true));
        equal("a.mmf", ListNames.display("a.mmf", "", true));
        equal("a.mmf", ListNames.display("a.mmf", "   ", true));
        equal("a.mmf", ListNames.display("a.mmf", null, true));
        equal("a.mmf", ListNames.display("a.mmf", null, false));
    }

    private static void titles() {
        byte[] small = file(cnti("ST:Blue Velocity,AN:Someone,"));
        equal("Blue Velocity", ListNames.embeddedTitle(small, small.length));
        // Credits embedded in the title are separated exactly as in the player heading.
        byte[] credited = file(cnti("ST:Song by Someone,AN:Someone,"));
        equal("Song", ListNames.embeddedTitle(credited, credited.length));
        equal("", ListNames.embeddedTitle(file(cnti("AN:Only Artist,")), 40));
        equal("", ListNames.embeddedTitle(new byte[0], 0));
        equal("", ListNames.embeddedTitle(null, 0));
        equal("", ListNames.embeddedTitle(ascii("not a smaf file at all"), 22));
    }

    private static void largeFileUsesOnlyThePrefix() {
        // A real file is mostly score data after CNTI; the title must be found
        // from the leading bytes alone, and a truncated chunk must not throw.
        byte[] score = new byte[1_000_000];
        Arrays.fill(score, (byte) 0x55);
        byte[] big = file(cnti("ST:Big Song,"), chunk("MTR\0", score));
        byte[] prefix = Arrays.copyOf(big, ListNames.MAX_PREFIX_BYTES);
        equal("Big Song", ListNames.embeddedTitle(prefix, prefix.length));
        for (int length = 0; length <= 64; length++) ListNames.embeddedTitle(big, length);
        checks++;
    }

    public static void main(String[] args) {
        display();
        titles();
        largeFileUsesOnlyThePrefix();
        System.out.println("List name tests PASS: " + checks + " assertions");
    }
}
