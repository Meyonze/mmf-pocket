package app.mmfpocket.player;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Random;

/** Original byte fixtures for display metadata; no private MMF files. */
public final class MmfMetadataTest {
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
    private static byte[] file(byte[]... chunks) { byte[] body=concat(chunks); return concat(ascii("MMMD"),u32(body.length+2),body,new byte[2]); }
    private static byte[] cnti(int code, byte[] text) { return chunk("CNTI",concat(new byte[]{0,0,(byte)code,0,0},text)); }
    private static byte[] field(String key, byte[] text) {
        return concat(ascii(key),new byte[]{(byte)(text.length>>8),(byte)text.length},text);
    }
    private static byte[] optional(int code, byte[]... fields) {
        return chunk("OPDA",chunk("Dch"+(char)code,concat(fields)));
    }
    private static void contents() {
        // ソ includes SJIS trail0x5c; it must not swallow the following character.
        byte[] song={(byte)0x83,0x5c,(byte)0x83,(byte)0x89,(byte)0x82,(byte)0xcc,(byte)0x89,(byte)0xcc};
        MmfMetadata m=MmfMetadata.read(file(cnti(0,concat(ascii("ST:"),song,
                ascii(",AN:Artist\\, Duo,SW:Composer,WW:Lyricist,AW:Arranger,")))));
        equal("ソラの歌",m.title()); equal("Artist, Duo",m.artist());
        equal("Composer",m.composer());equal("Lyricist",m.lyricist());equal("Arranger",m.arranger());
        m=MmfMetadata.read(file(cnti(0,ascii("ST:A\\\\B\\:C,AR:alias,AN:primary,"))));
        equal("A\\B:C",m.title());equal("primary",m.artist());
        equal("Final",MmfMetadata.read(file(cnti(0,ascii("ST:Final")))).title());
        equal("Before",MmfMetadata.read(file(cnti(0,ascii("ST:Before\0,AN:Hidden,")))).title());
        equal("",MmfMetadata.read(file(cnti(0,ascii("ST:Before\0,AN:Hidden,")))).artist());
        equal("First",MmfMetadata.read(file(cnti(0,ascii("ST:First,ST:Second,")))).title());
    }
    private static void optionalAndPriority() {
        byte[] contents=cnti(0,ascii("ST:Fallback,AN:CNTI artist,SW:CNTI composer,"));
        byte[] title={(byte)0x93,(byte)0xfa,(byte)0x96,0x7b,(byte)0x8c,(byte)0xea}; // 日本語
        byte[] opt=optional(0,field("ST",title),field("AR",ascii("Alias")),field("AN",ascii("Singer")),
                field("WW",ascii("Words")),field("AW",ascii("Arrange")),field("ZZ",ascii("ST:Fake,")));
        for(byte[] data:new byte[][]{file(contents,opt),file(opt,contents)}) {
            MmfMetadata m=MmfMetadata.read(data);
            equal("日本語",m.title());equal("Singer",m.artist());equal("CNTI composer",m.composer());
            equal("Words",m.lyricist());equal("Arrange",m.arranger());
        }
        equal("Fallback",MmfMetadata.read(file(contents,optional(0,field("ST",ascii("   "))))).title());
        equal("Ascii",MmfMetadata.read(file(cnti(99,ascii("ST:Ascii,")))).title());
        equal("",MmfMetadata.read(file(optional(99,field("ST",title)))).title());
        // Dch0 selects Shift_JIS even if CNTI has an unknown code type.
        equal("日本語",MmfMetadata.read(file(cnti(99,ascii("ST:Old,")),opt)).title());
    }
    private static void malformedAndBoundaries() {
        equal("",MmfMetadata.read(null).title());
        equal("",MmfMetadata.read(new byte[0]).title());
        equal("",MmfMetadata.read(ascii("RIFFxxxxxxxxST:Fake,")).title());
        byte[] original=file(cnti(0,ascii("ST:Good,")));
        for(int n=0;n<original.length;n++) MmfMetadata.read(Arrays.copyOf(original,n));
        equal("",MmfMetadata.read(file(chunk("MTR\0",ascii("ST:Fake,AN:Hidden,")))).title());
        equal("",MmfMetadata.read(file(optional(0,concat(ascii("ST"),new byte[]{0,100},ascii("X"))))).title());
        equal("Fallback",MmfMetadata.read(file(cnti(0,ascii("ST:Fallback,")),
                chunk("OPDA",concat(ascii("Dch\0"),u32(Integer.MAX_VALUE),ascii("STxxFake"))))).title());
        byte[] bounded=concat(ascii("MMMD"),u32(0),cnti(0,ascii("ST:Outside,")));
        equal("",MmfMetadata.read(bounded).title());
        equal("",MmfMetadata.read(file(chunk("CNTI",new byte[]{0,0,0,0}))).title());
        equal("Hi There",MmfMetadata.read(file(cnti(0,ascii("ST: Hi\nThere ,")))).title());
        String longTitle="a".repeat(1000);
        equal("a".repeat(256),MmfMetadata.read(file(cnti(0,ascii("ST:"+longTitle+",")))).title());
        equal("",MmfMetadata.read(file(cnti(0,new byte[65537]))).title());
        Random random=new Random(63);
        for(int i=0;i<3000;i++) {
            byte[] garbage=new byte[random.nextInt(1000)];random.nextBytes(garbage);
            MmfMetadata.read(garbage);MmfMetadata.read(file(chunk(i%2==0?"CNTI":"OPDA",garbage)));
        }
        equal("",MmfMetadata.EMPTY.title());equal("",MmfMetadata.EMPTY.artist());
    }
    public static void main(String[] args) {
        contents();optionalAndPriority();malformedAndBoundaries();
        System.out.println("MMF metadata tests PASS: "+checks+" assertions,3000 malformed fixtures and all truncated prefixes");
    }
}
