// SPDX-License-Identifier: Apache-2.0
package app.mmfpocket.player;

/** Same visual-envelope input, no decoder or Android dependencies. */
public final class MafuReactionTest {
    static void check(boolean ok,String reason){if(!ok)throw new AssertionError(reason);}
    public static void main(String[] args) {
        MafuAudioDance dance=new MafuAudioDance();float peak=0;
        for(int frame=0;frame<60*4;frame++) {
            long ms=frame*1000L/60;
            dance.update(1,ms,1f/60,new float[]{.4f,.3f,.2f,ms%400<40?.12f:0},0);
            peak=Math.max(peak,dance.pulse);
        }
        System.out.printf(java.util.Locale.ROOT,"Weak real attack peak visual pulse=%.6f%n",peak);
        check(peak>.12f&&peak<.4f,"weak authored attack remains visible but restrained in a moderate phrase");
        dance.reset();float[] attack={.4f,.3f,.2f,.12f};
        for(int frame=0;frame<60;frame++)dance.update(2,0,1f/60,attack,0);
        check(dance.pulse<.03,"same sampled window cannot retrigger");
        float prior=dance.pulse;
        for(int frame=0;frame<4;frame++)dance.update(2,40+frame*16,1f/60,attack,0);
        check(dance.pulse<=prior,"weak input respects160ms refractory interval");
        dance.update(2,200,1f/60,attack,0);
        check(dance.pulse>prior,"next genuinely later attack retriggers");
        for(int frame=0;frame<180;frame++)dance.update(2,220+frame*16,1f/60,new float[4],0);
        check(dance.pulse<.001&&dance.level<.001,"silence still settles despite visual gain");
        dance.update(3,0,0,new float[4],0);check(dance.pulse==0,"source change clears beat memory");
        System.out.println("PASS weak-attack visibility, held-window/160ms guard, silence and source reset. No sound output changed/tested here.");
    }
}
