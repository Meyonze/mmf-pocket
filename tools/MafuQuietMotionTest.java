package app.mmfpocket.player;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

/** Exercises actual View pose selection/math with host-only Android drawing stubs. */
public final class MafuQuietMotionTest {
    private static void check(boolean ok, String why) {
        if (!ok) throw new AssertionError(why);
    }
    private static float value(Object pose, String name) throws Exception {
        Field field = pose.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return field.getFloat(pose);
    }
    public static void main(String[] args) throws Exception {
        IllustratedMafuMascotView view = new IllustratedMafuMascotView(null);
        Field field = IllustratedMafuMascotView.class.getDeclaredField("audioDance");
        field.setAccessible(true);
        MafuAudioDance dance = (MafuAudioDance) field.get(view);
        Method audio = IllustratedMafuMascotView.class.getDeclaredMethod("calculateAudioMotion", float.class);
        Method stopped = IllustratedMafuMascotView.class.getDeclaredMethod("calculateMotion", float.class);
        audio.setAccessible(true);
        stopped.setAccessible(true);
        float minimum = 100, maximum = -100, minimumScale = 100, maximumScale = -100;
        float prior = 0, biggestStep = 0;
        for (int frame = 0; frame < 60 * 12; frame++) {
            float seconds = frame / 60f;
            Object pose = audio.invoke(view, seconds);
            float y = value(pose, "y");
            minimum = Math.min(minimum, y);
            maximum = Math.max(maximum, y);
            minimumScale = Math.min(minimumScale, value(pose, "scaleY"));
            maximumScale = Math.max(maximumScale, value(pose, "scaleY"));
            if (frame > 0) biggestStep = Math.max(biggestStep, Math.abs(y - prior));
            prior = y;
            check(value(pose, "soundPulse") == 0 && value(pose, "speedLines") == 0,
                    "silent breathing cannot pretend a detected beat");
            check(value(pose, "flipXDegrees") == 0 && value(pose, "turnYDegrees") == 0,
                    "silence must not start an acrobatic/model rotation");
        }
        check(maximum - minimum > 2 && maximum - minimum < 3,
                "silent playback has a visible but restrained breathing arc");
        check(maximumScale - minimumScale > .025f && biggestStep < .05f,
                "continuous breathing shape, no frame jumps");
        dance.level = .7f;
        dance.intensity = .8f;
        dance.pulse = .6f;
        dance.lean = .3f;
        Object music = audio.invoke(view, 1.25f);
        float activity = dance.level * (.22f + .78f * dance.intensity);
        float expectedY = (float)Math.sin(1.25f * .22f * Math.PI * 2) * .25f
                - activity * 1.6f - dance.pulse * 9.5f;
        check(Math.abs(value(music, "y") - expectedY) < .00001f,
                "audible energetic motion unchanged by silent breathing");
        Object paused = stopped.invoke(view, .9f);
        check(Math.abs(value(paused, "y")) <= .35f && Math.abs(value(paused, "scaleY") - 1) <= .0081f,
                "paused pose retains the existing subtler idle cue");
        System.out.printf(java.util.Locale.ROOT,
                "Silent playback y=%.3f..%.3f, scaleY=%.3f..%.3f, largest60Hz-step=%.4f%n",
                minimum, maximum, minimumScale, maximumScale, biggestStep);
        System.out.println("PASS actual View silent breathing, beat/acrobatics absence, smoothness, active/paused separation. No device appearance claim.");
    }
}
