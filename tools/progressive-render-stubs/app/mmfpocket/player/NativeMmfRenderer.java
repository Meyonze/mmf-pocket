// SPDX-License-Identifier: Apache-2.0
// Host-only PCM producer double, NOT native decoding or Android/JNI emulation.
// Explicit host javac source selection excludes the production native class.
package app.mmfpocket.player;
final class NativeMmfRenderer {
    enum Failure { NONE, CREATE, RENDER }
    static short[] samples;
    static int blockFrames, position, creates, renders, destroys;
    static boolean lastPhone;
    static Failure failure;
    static void configure(short[] pcm,int frames,Failure fault){
        samples=pcm.clone();blockFrames=frames;failure=fault;
        position=creates=renders=destroys=0;
    }
    static String createSession(byte[] input,boolean phone,long[] info){
        ++creates;lastPhone=phone;
        if(failure==Failure.CREATE)return "synthetic create failure";
        info[0]=1;info[1]=info[2]=samples.length/2;return "";
    }
    static int renderSession(long handle,short[] output){
        if(handle!=1)throw new AssertionError("Wrong synthetic handle");
        ++renders;if(failure==Failure.RENDER)return -1;
        int count=Math.min(Math.min(blockFrames,output.length/2),(samples.length-position)/2);
        System.arraycopy(samples,position,output,0,count*2);position+=count*2;return count;
    }
    static void destroySession(long handle){
        if(handle!=1)throw new AssertionError("Wrong destroyed handle");++destroys;
    }
}
