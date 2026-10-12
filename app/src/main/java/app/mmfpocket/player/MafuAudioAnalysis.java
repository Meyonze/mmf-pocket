package app.mmfpocket.player;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.net.Uri;
import android.os.Process;
import android.os.CancellationSignal;

import java.io.Closeable;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/** Optional WAV analysis, never queued on preparation/playback workers or the UI. */
final class MafuAudioAnalysis {
    private final Context context;
    private final Object streamLock = new Object();
    private final ExecutorService worker = backgroundExecutor("mafu-analysis");
    private final ExecutorService closer = backgroundExecutor("mafu-close");
    private Future<?> task;
    private Closeable activeStream;
    private CancellationSignal openSignal;

    MafuAudioAnalysis(Context context) { this.context = context.getApplicationContext(); }

    void start(MafuAudioVisualization.Source source, File file, Uri uri) {
        cancel();
        CancellationSignal signal = new CancellationSignal();
        openSignal = signal;
        task = worker.submit(() -> {
            if (source.cancelled) return;
            try {
                if (file != null) {
                    try (InputStream input = new FileInputStream(file)) { analyze(source, input); }
                } else {
                    // Own descriptor, not the one handed to MediaPlayer. This stream
                    // respects a provider's asset offset and declared/unknown length.
                    try (AssetFileDescriptor descriptor = context.getContentResolver()
                            .openAssetFileDescriptor(uri, "r", signal)) {
                        if (descriptor == null) throw new IOException("No visual source");
                        synchronized (streamLock) {
                            if (source.cancelled) return;
                            activeStream = descriptor;
                        }
                        try {
                            if (source.cancelled) return;
                            try (InputStream input = descriptor.createInputStream()) { analyze(source, input); }
                        } finally {
                            synchronized (streamLock) { if (activeStream == descriptor) activeStream = null; }
                        }
                    }
                }
            } catch (IOException | RuntimeException ignored) {
                // Unsupported/truncated/provider failures change only the animation.
                source.envelope.disable();
            }
        });
    }

    private void analyze(MafuAudioVisualization.Source source, InputStream input) throws IOException {
        synchronized (streamLock) {
            if (source.cancelled) return;
            activeStream = input;
        }
        try {
            boolean supported = MafuWavAnalysis.read(input, source.envelope,
                    () -> source.cancelled || Thread.currentThread().isInterrupted());
            if (!supported) source.envelope.disable();
        } finally {
            synchronized (streamLock) { if (activeStream == input) activeStream = null; }
        }
    }

    void cancel() {
        if (task != null) { task.cancel(true); task = null; }
        CancellationSignal signal = openSignal;
        openSignal = null;
        Closeable stream;
        synchronized (streamLock) { stream = activeStream; activeStream = null; }
        if (stream != null || signal != null) closer.execute(() -> {
            try {
                if (signal != null) signal.cancel();
            } catch (RuntimeException ignored) {
                // A provider's optional cancellation must not crash the app.
            } finally {
                try { if (stream != null) stream.close(); }
                catch (IOException | RuntimeException ignored) { /* Best effort provider cancellation. */ }
            }
        });
    }

    void release() { cancel(); worker.shutdownNow(); closer.shutdown(); }

    private static ExecutorService backgroundExecutor(String name) {
        return Executors.newSingleThreadExecutor(action -> {
            Thread thread = new Thread(() -> {
                Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);
                action.run();
            }, name);
            thread.setDaemon(true);
            return thread;
        });
    }
}
