package app.mmfpocket.player;

import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.util.function.BooleanSupplier;

/** Bounded, incremental visual-only RIFF reader; unsupported audio never affects playback. */
final class MafuWavAnalysis {
    private static final long MAX_HEADER_BYTES = 1_048_576;
    private static final int MAX_CHUNKS = 4096;

    static boolean read(InputStream input, MafuAudioEnvelope target,
                        BooleanSupplier cancelled) throws IOException {
        byte[] header = new byte[40];
        exact(input, header, 12);
        if (!id(header, 0, "RIFF") || !id(header, 8, "WAVE")) return false;
        long remaining = u32(header, 4) - 4;
        int format = 0, channels = 0, rate = 0, bits = 0, align = 0;
        long headerBytes = 12;
        for (int chunk = 0; chunk < MAX_CHUNKS && remaining >= 8; chunk++) {
            if (cancelled.getAsBoolean()) return false;
            exact(input, header, 8);
            remaining -= 8;
            long size = u32(header, 4), padded = size + (size & 1);
            if (padded > remaining) return false;
            if (id(header, 0, "data")) {
                if ((format != 1 && format != 3) || (channels != 1 && channels != 2)
                        || rate < 1000 || rate > 192000 || align != channels * (bits / 8)
                        || (format == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32)
                        || (format == 3 && bits != 32)) return false;
                MafuAudioEnvelope.Builder builder = new MafuAudioEnvelope.Builder(target, rate);
                byte[] buffer = new byte[4096 * align];
                long bytesLeft = size;
                while (bytesLeft >= align && !target.full() && !target.isDisabled()) {
                    if (cancelled.getAsBoolean()) return false;
                    int count = (int) Math.min(buffer.length, bytesLeft - bytesLeft % align);
                    exact(input, buffer, count);
                    for (int offset = 0; offset < count; offset += align) {
                        double left = decode(buffer, offset, bits, format);
                        double right = channels == 1 ? left : decode(buffer, offset + bits / 8, bits, format);
                        builder.add(left, right);
                    }
                    bytesLeft -= count;
                }
                if (cancelled.getAsBoolean() || target.isDisabled() || bytesLeft % align != 0) return false;
                builder.finish();
                return true;
            }
            headerBytes += 8 + padded;
            if (headerBytes > MAX_HEADER_BYTES) return false;
            if (id(header, 0, "fmt ")) {
                if (size < 16 || size > 40) return false;
                exact(input, header, (int) size);
                format = u16(header, 0); channels = u16(header, 2);
                long sampleRate = u32(header, 4);
                if (sampleRate > 192000) return false;
                rate = (int) sampleRate; align = u16(header, 12); bits = u16(header, 14);
                // WAVEFORMATEXTENSIBLE: accept only full PCM/IEEE_FLOAT subtype GUIDs.
                if (format == 0xfffe) {
                    if (size != 40 || u16(header, 16) != 22 || u16(header, 18) != bits) return false;
                    byte[] suffix = {0,0,0,0,0x10,0,(byte)0x80,0,0,(byte)0xaa,0,0x38,(byte)0x9b,0x71};
                    for (int i = 0; i < suffix.length; i++) if (header[26 + i] != suffix[i]) return false;
                    format = u16(header, 24);
                }
                if ((size & 1) != 0) skip(input, 1, cancelled);
            } else skip(input, padded, cancelled);
            remaining -= padded;
        }
        return false;
    }

    private static double decode(byte[] data, int offset, int bits, int format) {
        if (format == 3) return Float.intBitsToFloat((int) u32(data, offset));
        if (bits == 8) return ((data[offset] & 255) - 128) / 128.0;
        int value = 0;
        for (int i = 0; i < bits / 8; i++) value |= (data[offset + i] & 255) << (i * 8);
        if (bits < 32) value = value << (32 - bits) >> (32 - bits);
        return value / Math.scalb(1.0, bits - 1);
    }

    private static int u16(byte[] b, int o) { return (b[o] & 255) | ((b[o + 1] & 255) << 8); }
    private static long u32(byte[] b, int o) { return u16(b,o) | ((long)u16(b,o+2) << 16); }
    private static boolean id(byte[] b, int o, String text) {
        for (int i = 0; i < 4; i++) if ((b[o+i] & 255) != text.charAt(i)) return false;
        return true;
    }
    private static void exact(InputStream input, byte[] buffer, int count) throws IOException {
        int offset = 0;
        while (offset < count) {
            int read = input.read(buffer, offset, count - offset);
            if (read < 0) throw new EOFException("truncated visual WAV");
            if (read == 0) { int single = input.read(); if (single < 0) throw new EOFException(); buffer[offset++] = (byte)single; }
            else offset += read;
        }
    }
    private static void skip(InputStream input, long count, BooleanSupplier cancelled) throws IOException {
        while (count > 0) {
            if (cancelled.getAsBoolean()) throw new IOException("analysis cancelled");
            long skipped = input.skip(count);
            if (skipped > 0) count -= skipped;
            else { if (input.read() < 0) throw new EOFException(); --count; }
        }
    }
}
