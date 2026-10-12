// SPDX-License-Identifier: Apache-2.0
package app.mmfpocket.player;

/** Synthetic visual inputs and PCM: no private music, Android, or sound-output edits. */
public final class MafuIntensityTest {
    private static void check(boolean ok, String message) {
        if (!ok) throw new AssertionError(message);
    }

    private static final class Result {
        float intensity, pulse, largestStep;
        int actions;
    }

    private static Result phrase(float level, float onset, int interval, int fps) {
        MafuAudioDance dance = new MafuAudioDance();
        Result result = new Result();
        int previousAction = -1;
        for (int frame = 0; frame < fps * 30; frame++) {
            long position = frame * 1000L / fps;
            float prior = dance.intensity;
            dance.update(1, position, 1f / fps, new float[]{level, level * .7f, level * .4f,
                    interval > 0 && position % interval < 40 ? onset : 0}, 0);
            result.largestStep = Math.max(result.largestStep, Math.abs(dance.intensity - prior));
            if (dance.action >= 0 && previousAction < 0) result.actions++;
            previousAction = dance.action;
            if (position > 20_000) result.pulse = Math.max(result.pulse, dance.pulse);
        }
        result.intensity = dance.intensity;
        return result;
    }

    private static void strengthAndRhythm() {
        Result gentle = phrase(.8f, 1, 1800, 60);
        Result intense = phrase(.8f, 1, 400, 60);
        Result loudSustain = phrase(.95f, 1, 0, 60);
        Result quietFast = phrase(.16f, 1, 400, 60);
        System.out.printf(java.util.Locale.ROOT,
                "Intensity slow=%.3f fast=%.3f loud-sustain=%.3f quiet-fast=%.3f; pulse slow=%.3f fast=%.3f%n",
                gentle.intensity, intense.intensity, loudSustain.intensity, quietFast.intensity,
                gentle.pulse, intense.pulse);
        check(intense.intensity > .65f && intense.intensity > gentle.intensity + .3f,
                "same loudness, denser attacks must yield more expression");
        check(intense.pulse > gentle.pulse * 1.35f, "calm beats are gentler, not just slower");
        check(gentle.actions == 0 && loudSustain.actions == 0 && quietFast.actions == 0,
                "slow accents, loud sustained notes and quiet quick notes must not trigger acrobatics");
        check(loudSustain.intensity < .2f && quietFast.intensity < .15f,
                "volume alone or attack frequency alone must not imply excitement");
        check(intense.actions >= 2, "sustained exciting phrase retains flourish variety");
        check(intense.largestStep < .02f, "phrase intensity cannot jump at a frame boundary");
        float at30 = phrase(.8f, 1, 400, 30).intensity;
        float at120 = phrase(.8f, 1, 400, 120).intensity;
        check(Math.abs(at30 - at120) < .03f, "phrase estimate is stable across 30/60/120Hz");
    }

    private static void transitionsAndReset() {
        MafuAudioDance dance = new MafuAudioDance();
        float[] silence = new float[4];
        float maximumIsolated = 0;
        for (int frame = 0; frame < 60 * 8; frame++) {
            long position = frame * 1000L / 60;
            dance.update(1, position, 1f / 60, position < 80 ? new float[]{1, 1, 1, 1} : silence, 0);
            maximumIsolated = Math.max(maximumIsolated, dance.intensity);
            check(dance.action < 0, "one loud accent in silence must not start a flip");
        }
        check(maximumIsolated < .08f, "isolated accent does not establish phrase energy");
        for (int frame = 0; frame < 60 * 25; frame++) {
            long position = 8000 + frame * 1000L / 60;
            dance.update(1, position, 1f / 60, new float[]{.8f, .7f, .4f,
                    position % 400 < 40 ? 1 : 0}, 0);
        }
        check(dance.intensity > .65f, "calm to exciting passage builds intensity");
        float beforeFade = dance.intensity;
        dance.update(1, 33000, 1f / 60, silence, 0);
        check(dance.intensity > beforeFade - .02f, "quiet transition must not abruptly snap pose");
        for (int frame = 1; frame < 60 * 12; frame++) {
            long position = 33000 + frame * 1000L / 60;
            dance.update(1, position, 1f / 60, silence, 0);
        }
        check(dance.intensity < .03f && dance.action < 0 && dance.pulse < .001f,
                "quiet passage settles and an existing flourish finishes");
        dance.update(1, 0, 0, silence, 0);
        check(dance.intensity == 0 && dance.action < 0, "seek clears phrase history");
        dance.update(2, 0, 0, silence, 0);
        check(dance.intensity == 0, "new track clears phrase history");
        dance.update(2, 40, Float.NaN, new float[]{Float.NaN, Float.POSITIVE_INFINITY, -1, Float.NaN}, 0);
        check(Float.isFinite(dance.intensity) && Float.isFinite(dance.pulse), "invalid values stay bounded");
        // Fixed playback position must not count the same measured attack repeatedly.
        for (int frame = 0; frame < 60 * 20; frame++) {
            dance.update(3, 0, 1f / 60, new float[]{1, 1, 1, 1}, 0);
            check(dance.action < 0, "held position cannot invent a fast rhythm");
        }
        check(dance.intensity < .2f, "held sample has no repeated attack activity");
        dance.reset();
        check(dance.intensity == 0 && dance.pulse == 0, "pause/reset clears all intensity history");
    }

    private static float pcmPhrase(boolean exciting) {
        MafuAudioEnvelope envelope = new MafuAudioEnvelope();
        MafuAudioEnvelope.Builder builder = new MafuAudioEnvelope.Builder(envelope, 8000);
        for (int sample = 0; sample < 8000 * 24; sample++) {
            double seconds = sample / 8000.0;
            double beatAge = seconds % (exciting ? .4 : 1.8);
            double attack = Math.exp(-beatAge / .075);
            double signal = (exciting ? .17 : .09) * Math.sin(2 * Math.PI * 330 * seconds)
                    + (exciting ? .48 : .12) * attack * Math.sin(2 * Math.PI * 70 * seconds);
            builder.add(signal, -signal); // Opposite-phase stereo still carries visual energy.
        }
        builder.finish();
        MafuAudioDance dance = new MafuAudioDance();
        float[] values = new float[4];
        for (int frame = 0; frame < 60 * 24; frame++) {
            long position = frame * 1000L / 60;
            check(envelope.sample(position, values), "synthetic PCM analysis has complete windows");
            dance.update(5, position, 1f / 60, values, 0);
        }
        return dance.intensity;
    }

    public static void main(String[] args) {
        strengthAndRhythm();
        transitionsAndReset();
        float quiet = pcmPhrase(false), exciting = pcmPhrase(true);
        System.out.printf(java.util.Locale.ROOT, "Actual PCM/envelope intensity quiet=%.3f exciting=%.3f%n", quiet, exciting);
        check(exciting > quiet + .2f, "actual unchanged PCM analyzer distinguishes phrase strength");
        System.out.println("PASS phrase intensity/rhythm, gradual transitions, flourish gating, rates, reset, silence and synthetic PCM. No device or mood-recognition claim.");
    }
}
