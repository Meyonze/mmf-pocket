package app.mmfpocket.player;

import java.io.ByteArrayOutputStream;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;

public final class SongDisplayTextTest {
    private static int checks;
    private static void expect(String title, String byline, CharSequence raw, CharSequence... credits) {
        SongDisplayText actual = SongDisplayText.from(raw, credits);
        checks++;
        if (!title.equals(actual.title) || !byline.equals(actual.byline)) {
            throw new AssertionError("Expected [" + title + "] / [" + byline + "] got ["
                    + actual.title + "] / [" + actual.byline + "]");
        }
    }
    private static byte[] join(byte[]... arrays) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (byte[] array : arrays) out.writeBytes(array);
        return out.toByteArray();
    }
    private static byte[] ascii(String text) { return text.getBytes(StandardCharsets.US_ASCII); }
    private static byte[] u32(int n) { return new byte[]{(byte)(n>>24),(byte)(n>>16),(byte)(n>>8),(byte)n}; }
    private static byte[] chunk(String tag, byte[] body) { return join(ascii(tag),u32(body.length),body); }
    private static byte[] score(boolean optional, String heading, String artist) {
        Charset sjis = Charset.forName("windows-31j");
        byte[] metadata;
        if (optional) {
            byte[] value=heading.getBytes(sjis);
            byte[] field=join(ascii("ST"),new byte[]{(byte)(value.length>>8),(byte)value.length},value);
            metadata=chunk("OPDA",chunk("Dch\0",field));
        } else {
            metadata=chunk("CNTI",join(new byte[5],("ST:"+heading+",AN:"+artist+",").getBytes(sjis)));
        }
        return join(ascii("MMMD"),u32(metadata.length+2),metadata,new byte[2]);
    }
    public static void main(String[] args) {
        // Transcribed from the reported screenshot: one combined ST, no AN.
        String screenshot="いつか終わる夢 by 全てを捨てし者 / game-melody.com";
        expect("いつか終わる夢", "by 全てを捨てし者 / game-melody.com", screenshot);
        expect("いつか終わる夢", "by 全てを捨てし者 / game-melody.com", screenshot,
                "全てを捨てし者", "game-melody.com");
        for (boolean optional : new boolean[]{false,true}) {
            MmfMetadata metadata=MmfMetadata.read(score(optional,screenshot,""));
            if (!metadata.title().equals(screenshot)) throw new AssertionError("Do not rewrite source tags");
            expect("いつか終わる夢", "by 全てを捨てし者 / game-melody.com",
                    metadata.title(),metadata.artist(),metadata.composer(),metadata.lyricist());
        }
        expect("曲名", "by 作者", "曲名 by 作者");
        expect("曲名", "by 作者", "曲名 BY 作者");
        expect("曲名", "by 作者", "曲名　by　作者");
        expect("Song", "by Alice / Bob", "Song by Alice / Bob", "Alice", "Bob");
        expect("Song", "by Alice", "Song by Alice", "Alice");
        expect("Song", "by Alice / https://example.com/music/1", "Song by Alice / https://example.com/music/1");
        expect("Song", "by example.com", "Song by example.com");
        expect("Stand by Me", "", "Stand by Me");
        expect("Stand by Me", "by Ben E. King", "Stand by Me", "Ben E. King");
        expect("Stand by Me", "by 作者", "Stand by Me by 作者");
        expect("İstanbul", "by 作者", "İstanbul by 作者");
        expect("Bye Bye", "by Artist", "Bye Bye", "Artist");
        expect("By the Way", "", "By the Way");
        expect("Title by", "", "Title by");
        expect("by Artist", "", "by Artist");
        // Credits-only ST fields must leave room for the filename fallback.
        expect("", "by \uFF8E\uFF9E\uFF9D\uFF7C\uFF9E\uFF6C\uFF6F\uFF78 /game-melody.com", "by \uFF8E\uFF9E\uFF9D\uFF7C\uFF9E\uFF6C\uFF6F\uFF78 /game-melody.com");
        expect("", "by 作者 / game-melody.com", "　by 作者 / game-melody.com　");
        expect("", "by 作者", "BY　作者");
        expect("", "by Alice", "by Alice", "Alice");
        expect("", "by Alice / Bob", "by Alice / Bob");
        expect("", "by 作者 / サイト", "by 作者 / サイト", "作者", "サイト");
        for (boolean optional : new boolean[]{false,true}) {
            MmfMetadata metadata=MmfMetadata.read(score(optional,"by 作者 /game-melody.com",""));
            expect("", "by 作者 /game-melody.com", metadata.title(),metadata.artist());
        }
        expect("Title by", "", "Title by　".stripTrailing());
        expect("Title", "by Artist / Writer", "Title", "Artist", "Artist", "Writer", null, " ");
        expect("Track.mmf", "", "Track.mmf");
        expect("", "", null);
        System.out.println("Song display tests PASS: "+checks+" cases; screenshot ST-only CNTI/OPDA, separate credits, deduplication, English-title guards");
    }
}
