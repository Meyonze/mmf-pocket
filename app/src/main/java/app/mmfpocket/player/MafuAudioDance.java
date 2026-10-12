package app.mmfpocket.player;

/** Display-rate smoothing and beat-driven flourishes; independent of Android and audio output. */
final class MafuAudioDance {
    float level, bass, treble, pulse, phase, lean, intensity;
    int action = -1;
    float actionSeconds;
    private long token, lastWindow = -1, lastPosition = -1;
    private float sinceFlourish = 5;
    private float beatStrength, jumpAge = 1, jumpDuration = .38f;
    private int nextAction;
    private long lastAttackMs = -1000, lastJumpMs = -2000;
    private float leanTarget = -1;
    private float phraseEnergy, attackActivity;
    private float recentAttackPeak, jumpIntervalMs = 520;
    private boolean flourishReady;

    void reset() {
        level = bass = treble = pulse = beatStrength = phase = actionSeconds = lean = intensity = 0;
        phraseEnergy = attackActivity = 0;
        recentAttackPeak = 0; jumpIntervalMs = 520; jumpAge = 1; jumpDuration = .38f;
        flourishReady = false;
        action = -1; token = 0; lastWindow = lastPosition = -1;
        sinceFlourish = 5;
        lastAttackMs = -1000; lastJumpMs = -2000;
        leanTarget = -1;
    }

    void update(long sourceToken, long positionMs, float elapsed, float[] values, int seed) {
        if (sourceToken != token || lastPosition >= 0
                && (positionMs < lastPosition - 80 || positionMs > lastPosition + 300)) {
            reset(); token = sourceToken; nextAction = Math.floorMod(seed, 4);
        }
        lastPosition = positionMs;
        float dt = Float.isFinite(elapsed) ? Math.max(0, Math.min(.05f, elapsed)) : 0;
        level = smooth(level, values[0], dt);
        bass = smooth(bass, values[1], dt);
        treble = smooth(treble, values[2], dt);
        // A loud sustained chord is not a fast rhythm. Keep a short phrase history
        // of both energy and distinct attacks; never alter the underlying audio.
        phraseEnergy = approach(phraseEnergy, bounded(values[0]), dt, 1.6f);
        attackActivity *= (float) Math.exp(-dt / 2.2f);
        recentAttackPeak *= (float) Math.exp(-dt / 2.5f);
        sinceFlourish += dt;
        long window = positionMs / MafuAudioEnvelope.WINDOW_MS;
        if (window != lastWindow) {
            lastWindow = window;
            float onset = bounded(values[3]);
            recentAttackPeak = Math.max(recentAttackPeak, onset);
            // Dense small attacks still contribute to phrase strength, not every jump.
            if (onset > .06f && positionMs - lastAttackMs >= 160) {
                lastAttackMs = positionMs;
                attackActivity = Math.min(1.5f, attackActivity
                        + (.2f + .8f * (float)Math.sqrt(onset)) / (2.2f * 3f));
            }
            // Follow prominent measured accents at a calmer cadence, never invent beats.
            // A short recovery floor deliberately gives very fast patterns a half-time feel.
            float recoveryMs = Math.max(420, Math.min(800, jumpIntervalMs * .7f));
            long gap = positionMs - lastJumpMs;
            if (onset > .06f && onset >= recentAttackPeak * .58f
                    && bounded(values[0]) > .01f && gap >= recoveryMs && jumpAge >= jumpDuration) {
                if (lastJumpMs >= 0 && gap <= 1600) {
                    jumpIntervalMs += .25f * (gap - jumpIntervalMs);
                } else jumpIntervalMs = 520;
                lastJumpMs = positionMs;
                beatStrength = Math.min(1f, .15f + (float)Math.sqrt(onset) * 1.55f);
                jumpAge = 0;
                jumpDuration = Math.max(.34f, Math.min(.64f,
                        Math.max(420, Math.min(800, jumpIntervalMs * .7f)) * .00085f));
                leanTarget = -leanTarget;
                if (action < 0 && flourishReady && sinceFlourish > 7 && level > .18f && onset > .23f) {
                    action = Math.floorMod(nextAction++, 4);
                    actionSeconds = 0;
                    sinceFlourish = 0;
                }
            }
        }
        float energy = Math.max(0, Math.min(1, (phraseEnergy - .1f) / .65f));
        float targetIntensity = energy * (.16f + .84f * Math.min(1, attackActivity));
        intensity = approach(intensity, targetIntensity, dt, targetIntensity > intensity ? .9f : 1.8f);
        // Hysteresis avoids enabling acrobatics on a single accent or boundary jitter.
        flourishReady = intensity > .62f || flourishReady && intensity > .45f;
        jumpAge += dt;
        // Complete a smooth takeoff/landing before the next accepted accent.
        float arc = jumpAge >= jumpDuration ? 0 : (float)Math.sin(Math.PI * jumpAge / jumpDuration);
        pulse = beatStrength * (.25f + .75f * intensity) * arc * arc;
        lean += (leanTarget * pulse - lean) * (float)(1 - Math.exp(-dt / .1));
        phase += dt * (.18f + level * (.12f + .53f * intensity));
        if (action >= 0) {
            actionSeconds += dt;
            if (actionSeconds >= duration(action)) action = -1;
        }
    }

    static float duration(int action) {
        return action == 0 ? 2.1f : action == 1 ? 2.8f : action == 2 ? 3.2f : 1.8f;
    }

    private static float smooth(float value, float target, float dt) {
        float safe = bounded(target);
        return approach(value, safe, dt, safe > value ? .055f : .22f);
    }

    private static float bounded(float value) {
        return Float.isFinite(value) ? Math.max(0, Math.min(1, value)) : 0;
    }

    private static float approach(float value, float target, float dt, float seconds) {
        return value + (target - value) * (float)(1 - Math.exp(-dt / seconds));
    }
}
