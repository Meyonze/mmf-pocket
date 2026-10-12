package app.mmfpocket.player;

import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

/** Same-process, read-only bridge. No Context, player or View is retained here. */
final class MafuAudioVisualization {
    private static final AtomicLong sequence = new AtomicLong();
    private static final AtomicReference<Source> active = new AtomicReference<>();

    static final class Source {
        final long token = sequence.incrementAndGet();
        final String mediaId;
        final MafuAudioEnvelope envelope = new MafuAudioEnvelope();
        volatile boolean cancelled;
        Source(String mediaId) { this.mediaId = mediaId; }
        boolean sample(long positionMs, float[] output) {
            return !cancelled && envelope.sample(positionMs, output) && !cancelled;
        }
    }

    static Source start(String mediaId) {
        Source source = new Source(mediaId);
        Source previous = active.getAndSet(source);
        if (previous != null) previous.cancelled = true;
        return source;
    }

    static void clear(Source source) {
        if (source == null) return;
        source.cancelled = true;
        source.envelope.disable();
        active.compareAndSet(source, null);
    }

    static Source find(String mediaId, long token) {
        Source source = active.get();
        return source != null && !source.cancelled && source.token == token
                && source.mediaId.equals(mediaId) ? source : null;
    }
}
