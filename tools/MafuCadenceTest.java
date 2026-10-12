// SPDX-License-Identifier: Apache-2.0
package app.mmfpocket.player;

/** Cadence thinning and prominent accents, not a tempo/meter/melody detector. */
public final class MafuCadenceTest {
    private static void check(boolean ok, String reason) {
        if (!ok) throw new AssertionError(reason);
    }
    private static final class Result {
        int turns;
        float peak, largestStep;
    }
    private static Result pattern(int fps, int mainInterval, boolean ornaments) {
        MafuAudioDance dance = new MafuAudioDance();
        Result result = new Result();
        int priorSign = 0;
        float priorPulse = 0;
        for (int frame = 0; frame < fps * 24; frame++) {
            long position = frame * 1000L / fps;
            float onset = position % mainInterval < 40 ? 1
                    : ornaments && position % 80 < 40 ? .12f : 0;
            dance.update(1, position, 1f / fps, new float[]{.8f,.6f,.3f,onset}, 0);
            if (Math.abs(dance.lean) > .01f) {
                int sign = dance.lean > 0 ? 1 : -1;
                if (sign != priorSign) result.turns++;
                priorSign = sign;
            }
            result.peak = Math.max(result.peak, dance.pulse);
            result.largestStep = Math.max(result.largestStep, Math.abs(dance.pulse - priorPulse));
            priorPulse = dance.pulse;
        }
        return result;
    }
    private static void prominenceAndCadence() {
        Result dense = pattern(60,160,false);
        System.out.printf(java.util.Locale.ROOT,"Dense160ms pattern: %d visible directional beats in24s, peak=%.3f maxstep=%.3f%n",
                dense.turns,dense.peak,dense.largestStep);
        check(dense.turns > 25 && dense.turns <= 58,
                "dense attacks must thin to a calmer visible cadence, not react every160ms");
        check(dense.peak > .6f && dense.largestStep < .16f,
                "thinned strong beats keep expression and smooth flight");
        Result main = pattern(60,480,false), decorated = pattern(60,480,true);
        System.out.printf(java.util.Locale.ROOT,"480ms accents: %d turns; same accents with80ms ornaments: %d turns%n",
                main.turns,decorated.turns);
        check(Math.abs(main.turns - decorated.turns) <= 1,
                "quiet ornaments must not steal or add prominent accented jumps");
        check(decorated.turns >= 45 && decorated.turns <= 51,
                "prominent accents retained even with dense quiet accompaniment");
        Result at30 = pattern(30,480,true), at120 = pattern(120,480,true);
        check(Math.abs(at30.turns - at120.turns) <= 1,
                "cadence selection stable at30/60/120Hz");
        check(pattern(60,960,false).turns >= 23, "slower measured accents are not skipped");
    }
    private static void silenceResetAndRecovery() {
        MafuAudioDance dance = new MafuAudioDance();
        float[] silence = new float[4];
        float[] accent = {.8f,.6f,.3f,1};
        float prior = 0, largestStep = 0;
        for (int frame = 0; frame < 60 * 6; frame++) {
            long position = frame * 1000L / 60;
            dance.update(2,position,1f/60,position < 40 ? accent : silence,0);
            largestStep = Math.max(largestStep,Math.abs(dance.pulse-prior));
            prior = dance.pulse;
            check(dance.action < 0, "isolated accent still cannot cause acrobatics");
            if (position > 700) check(dance.pulse == 0, "no invented periodic jumps after one accent");
        }
        check(largestStep < .08f, "quiet isolated accent remains smooth");
        dance.update(3,0,1f/60,accent,0);
        check(dance.pulse > 0, "new source does not inherit previous recovery clock");
        dance.update(3,16,0,silence,0);
        dance.update(3,200,1f/60,accent,0);
        for (int frame = 0; frame < 42; frame++) dance.update(3,216+frame*16,1f/60,silence,0);
        check(dance.pulse == 0, "sub-beat200ms accent cannot restart a completed hop");
        dance.update(3,0,0,silence,0);
        check(dance.pulse == 0 && dance.lean == 0, "backward seek clears cadence and pose");
        dance.update(3,40,1f/60,accent,0);
        check(dance.pulse > 0, "accent immediately after seek uses clean history");
        dance.reset();
        check(dance.pulse == 0 && dance.intensity == 0, "pause/reset remains clean");
        for (int frame = 0; frame < 5; frame++) {
            dance.update(4,frame*16,1f/60,frame==0 ? accent : new float[]{.8f,.6f,.3f,0},0);
        }
        dance.update(4,250,.05f,new float[]{.8f,.6f,.3f,0},0);
        dance.update(4,500,.05f,accent,0);
        check(dance.pulse > .15f, "slow display clock cannot restart an unfinished hop at its feet");
    }
    public static void main(String[] args) {
        prominenceAndCadence();
        silenceResetAndRecovery();
        System.out.println("PASS accent prominence, cadence thinning, continuous hop, rates, silence and reset. No sound-output/device/exact beat-grid claim.");
    }
}
