package app.mmfpocket.player;

/** Single producer, lock-free readers. Visual measurements only; never modifies PCM. */
final class MafuAudioEnvelope {
    static final int WINDOW_MS = 40;
    static final int MAX_WINDOWS = 90_000; // One hour; later playback stays unaffected.
    private static final int PAGE_WINDOWS = 256;
    private final float[][] pages = new float[(MAX_WINDOWS + PAGE_WINDOWS - 1) / PAGE_WINDOWS][];
    private volatile int published;
    private volatile boolean disabled;

    boolean sample(long positionMs, float[] output) {
        if (disabled || positionMs < 0 || output.length < 4) return false;
        long window = positionMs / WINDOW_MS;
        int count = published; // Acquire all completed page/sample writes.
        if (window >= count) return false;
        int index = (int) window;
        float[] page = pages[index / PAGE_WINDOWS];
        System.arraycopy(page, (index % PAGE_WINDOWS) * 4, output, 0, 4);
        return !disabled;
    }

    void disable() { disabled = true; }
    boolean isDisabled() { return disabled; }
    int size() { return published; }
    boolean full() { return published >= MAX_WINDOWS; }

    private void append(float level, float bass, float treble, float onset) {
        if (disabled || full()) return;
        int index = published;
        int pageIndex = index / PAGE_WINDOWS;
        if (pages[pageIndex] == null) pages[pageIndex] = new float[PAGE_WINDOWS * 4];
        float[] page = pages[pageIndex];
        int offset = index % PAGE_WINDOWS * 4;
        page[offset] = level;
        page[offset + 1] = bass;
        page[offset + 2] = treble;
        page[offset + 3] = onset;
        published = index + 1; // Release only complete windows, never mutable PCM arrays.
    }

    static final class Builder {
        private final MafuAudioEnvelope target;
        private final int unitsPerWindow;
        private int windowUnits;
        private final double lowAlpha, highAlpha;
        private double lowLeft, lowRight, highLeft, highRight;
        private double energy, lowEnergy, highEnergy;
        private int frames, lastBeat = -10;
        private double previousRms, previousBass, average;

        Builder(MafuAudioEnvelope target, int sampleRate) {
            if (sampleRate < 1000 || sampleRate > 192000) throw new IllegalArgumentException("sample rate");
            this.target = target;
            unitsPerWindow = sampleRate * WINDOW_MS;
            lowAlpha = 1 - Math.exp(-2 * Math.PI * 180 / sampleRate);
            highAlpha = 1 - Math.exp(-2 * Math.PI * 2200 / sampleRate);
        }

        void acceptStereo16(short[] pcm, int count) {
            if (count < 0 || count > pcm.length / 2) throw new IllegalArgumentException("PCM bounds");
            if (target.disabled || target.full()) return;
            for (int i = 0; i < count; i++) {
                add(pcm[i * 2] / 32768.0, pcm[i * 2 + 1] / 32768.0);
            }
        }

        void add(double left, double right) {
            if (target.disabled || target.full()) return;
            if (!Double.isFinite(left) || !Double.isFinite(right)) { target.disable(); return; }
            left = Math.max(-1, Math.min(1, left));
            right = Math.max(-1, Math.min(1, right));
            lowLeft += lowAlpha * (left - lowLeft);
            lowRight += lowAlpha * (right - lowRight);
            highLeft += highAlpha * (left - highLeft);
            highRight += highAlpha * (right - highRight);
            double highL = left - highLeft, highR = right - highRight;
            // Combine energies, not stereo samples: opposite phase must not disappear.
            energy += (left * left + right * right) * .5;
            lowEnergy += (lowLeft * lowLeft + lowRight * lowRight) * .5;
            highEnergy += (highL * highL + highR * highR) * .5;
            ++frames;
            windowUnits += 1000;
            if (windowUnits >= unitsPerWindow) {
                windowUnits -= unitsPerWindow;
                publish();
            }
        }

        void finish() { if (frames > 0 && !target.disabled && !target.full()) publish(); }

        private void publish() {
            double rms = Math.sqrt(energy / frames);
            double bass = Math.sqrt(lowEnergy / frames);
            double treble = Math.sqrt(highEnergy / frames);
            double flux = Math.max(0, bass - previousBass) * 2
                    + Math.max(0, rms - previousRms) * .6;
            double onset = rms < .0001 ? 0 : Math.min(1, flux / (.025 + average * 1.5));
            int index = target.size();
            if (index - lastBeat < 4) onset = 0;
            if (onset > .2) lastBeat = index;
            target.append(display(rms), display(bass), display(treble), (float) onset);
            average += .08 * (rms - average);
            previousRms = rms;
            previousBass = bass;
            frames = 0;
            energy = lowEnergy = highEnergy = 0;
        }

        private static float display(double rms) {
            return rms < .0001 ? 0 : (float) Math.min(1, Math.sqrt(rms * 4));
        }
    }
}
