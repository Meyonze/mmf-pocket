// SPDX-License-Identifier: Apache-2.0
// Playback-only host tests must never decode/render native audio.
package app.mmfpocket.player;
final class NativeMmfRenderer {
    static String createSession(byte[] data,boolean phone,long[] info){throw new AssertionError("Native render forbidden");}
    static int renderSession(long handle,short[] pcm){throw new AssertionError("Native render forbidden");}
    static void destroySession(long handle){throw new AssertionError("Native render forbidden");}
}
