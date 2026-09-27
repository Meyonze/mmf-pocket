package app.mmfpocket.player;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

/** Renders a WAV cache at full speed while AudioTrack follows the completed prefix. */
final class ProgressivePlayback {
    static final int SAMPLE_RATE = 44100;
    private static final int CHANNELS = 2;
    private static final int BYTES_PER_FRAME = 4;
    private static final int BLOCK_FRAMES = 1024;
    private static final long PREBUFFER_FRAMES = SAMPLE_RATE / 2;

    interface Listener {
        void onReady();
        void onCacheReady(File cached);
        void onCompleted();
        void onError(String message);
    }

    private final Object lock = new Object();
    private final byte[] mmfData;
    private final boolean phoneSpeakerMode;
    private final File partialFile;
    private final File cachedFile;
    private final Listener listener;

    private volatile boolean cancelled;
    private volatile boolean paused;
    private volatile boolean ready;
    private volatile boolean renderingComplete;
    private volatile boolean playbackComplete;
    private volatile boolean buffering = true;
    private volatile boolean failed;
    private volatile long renderedFrames;
    private volatile long finalFrames;
    private volatile long scoreEndFrames;
    private volatile long upperBoundFrames;
    private volatile long trackBaseFrame;
    private volatile long submittedFrame;
    private volatile long lastKnownPosition;
    private volatile long requestedSeek = -1;
    private volatile File readableFile;
    private volatile AudioTrack audioTrack;
    private volatile float volume = 1f;
    private Thread renderThread;
    private Thread playbackThread;

    ProgressivePlayback(byte[] mmfData, boolean phoneSpeakerMode,
                        File partialFile, File cachedFile, Listener listener) {
        this.mmfData = mmfData;
        this.phoneSpeakerMode = phoneSpeakerMode;
        this.partialFile = partialFile;
        this.cachedFile = cachedFile;
        this.listener = listener;
        readableFile = partialFile;
    }

    void start() {
        renderThread = new Thread(this::renderLoop, "mmf-render");
        playbackThread = new Thread(this::playbackLoop, "mmf-playback");
        renderThread.start();
        playbackThread.start();
    }

    void cancel() {
        cancelled = true;
        synchronized (lock) {
            lock.notifyAll();
        }
        AudioTrack track = audioTrack;
        if (track != null) {
            try {
                track.pause();
                track.flush();
            } catch (IllegalStateException ignored) {
                // The playback thread may already be releasing it.
            }
        }
        if (renderThread != null) renderThread.interrupt();
        if (playbackThread != null) playbackThread.interrupt();
    }

    void pause() {
        if (playbackComplete) return;
        paused = true;
        if (!ready) return;
        AudioTrack track = audioTrack;
        if (track != null) {
            try {
                track.pause();
            } catch (IllegalStateException ignored) {
                // A simultaneous seek may be replacing the track.
            }
        }
    }

    void setVolume(float value) {
        volume = Math.max(0f, Math.min(1f, value));
        AudioTrack track = audioTrack;
        if (track != null) {
            try {
                track.setVolume(volume);
            } catch (IllegalStateException ignored) {
                // The playback thread may be replacing the track during a seek.
            }
        }
    }

    void resume() {
        if (!ready || playbackComplete) return;
        paused = false;
        AudioTrack track = audioTrack;
        if (track != null && submittedFrame > trackBaseFrame) {
            try {
                track.play();
            } catch (IllegalStateException ignored) {
                // The playback loop will start a replacement after a seek.
            }
        }
        synchronized (lock) {
            lock.notifyAll();
        }
    }

    long seekTo(long requestedFrame) {
        long clamped = Math.max(0, Math.min(requestedFrame, renderedFrames));
        requestedSeek = clamped;
        playbackComplete = false;
        AudioTrack track = audioTrack;
        if (track != null) {
            try {
                track.pause();
                track.flush();
            } catch (IllegalStateException ignored) {
                // The playback loop owns final release.
            }
        }
        synchronized (lock) {
            lock.notifyAll();
        }
        return clamped;
    }

    boolean isReady() {
        return ready;
    }

    boolean isPlaying() {
        return ready && !paused && !playbackComplete && !cancelled && !failed;
    }

    boolean isBuffering() {
        return buffering && !renderingComplete;
    }

    boolean isPlaybackComplete() {
        return playbackComplete;
    }

    boolean isRenderingComplete() {
        return renderingComplete;
    }

    long getRenderedFrames() {
        return renderedFrames;
    }

    long getTimelineFrames() {
        if (renderingComplete && finalFrames > 0) return finalFrames;
        return Math.max(1, Math.max(scoreEndFrames, renderedFrames));
    }

    long getPositionFrames() {
        AudioTrack track = audioTrack;
        if (track != null && ready) {
            try {
                long played = Integer.toUnsignedLong(track.getPlaybackHeadPosition());
                long position = Math.min(submittedFrame, trackBaseFrame + played);
                lastKnownPosition = Math.max(0, position);
            } catch (IllegalStateException ignored) {
                // Use the last sample position while replacing/releasing a track.
            }
        }
        return Math.min(lastKnownPosition, getTimelineFrames());
    }

    private void renderLoop() {
        long handle = 0;
        boolean cacheReady = false;
        try (RandomAccessFile output = new RandomAccessFile(partialFile, "rw")) {
            output.setLength(0);
            writeWavHeader(output, 0);

            long[] info = new long[3];
            String error = NativeMmfRenderer.createSession(mmfData, phoneSpeakerMode, info);
            if (error == null) error = "";
            if (!error.isEmpty() || info[0] == 0) {
                fail(error.isEmpty() ? "再生セッションを作成できません" : error);
                return;
            }
            handle = info[0];
            scoreEndFrames = Math.max(1, info[1]);
            upperBoundFrames = Math.max(scoreEndFrames, info[2]);
            synchronized (lock) {
                lock.notifyAll();
            }

            short[] pcm = new short[BLOCK_FRAMES * CHANNELS];
            byte[] bytes = new byte[BLOCK_FRAMES * BYTES_PER_FRAME];
            int peak = 0;
            while (!cancelled) {
                int count = NativeMmfRenderer.renderSession(handle, pcm);
                if (count < 0) {
                    fail("音声の生成中にエラーが発生しました");
                    return;
                }
                if (count == 0) break;
                int samples = count * CHANNELS;
                for (int i = 0; i < samples; ++i) {
                    int value = pcm[i];
                    peak = Math.max(peak, Math.abs(value));
                    bytes[i * 2] = (byte) (value & 0xff);
                    bytes[i * 2 + 1] = (byte) ((value >>> 8) & 0xff);
                }
                output.write(bytes, 0, count * BYTES_PER_FRAME);
                synchronized (lock) {
                    renderedFrames += count;
                    lock.notifyAll();
                }
            }
            if (cancelled) return;
            if (renderedFrames == 0 || peak < 4) {
                fail("音声データを生成できませんでした");
                return;
            }

            finalFrames = renderedFrames;
            writeWavHeader(output, finalFrames);
            output.getFD().sync();
            output.close();
            synchronized (lock) {
                Files.move(partialFile.toPath(), cachedFile.toPath(),
                        StandardCopyOption.REPLACE_EXISTING);
                readableFile = cachedFile;
                cacheReady = true;
                renderingComplete = true;
                lock.notifyAll();
            }
            listener.onCacheReady(cachedFile);
        } catch (Exception error) {
            if (!cancelled) {
                String message = error.getMessage() == null
                        ? error.getClass().getSimpleName() : error.getMessage();
                fail("変換に失敗しました: " + message);
            }
        } finally {
            if (handle != 0) NativeMmfRenderer.destroySession(handle);
            if (!cacheReady) {
                //noinspection ResultOfMethodCallIgnored
                partialFile.delete();
            }
            synchronized (lock) {
                lock.notifyAll();
            }
        }
    }

    private void playbackLoop() {
        AudioTrack track = null;
        RandomAccessFile input = null;
        long cursor = 0;
        boolean trackStarted = false;
        try {
            synchronized (lock) {
                while (!cancelled && !failed
                        && renderedFrames < PREBUFFER_FRAMES && !renderingComplete) {
                    lock.wait();
                }
            }
            if (cancelled || failed) return;

            synchronized (lock) {
                input = new RandomAccessFile(readableFile, "r");
            }
            track = createAudioTrack();
            audioTrack = track;
            trackBaseFrame = 0;
            submittedFrame = 0;
            ready = true;
            listener.onReady();

            byte[] bytes = new byte[BLOCK_FRAMES * BYTES_PER_FRAME];
            while (!cancelled && !failed) {
                long seek = requestedSeek;
                if (seek >= 0) {
                    requestedSeek = -1;
                    releaseTrack(track);
                    track = createAudioTrack();
                    audioTrack = track;
                    cursor = Math.min(seek, renderedFrames);
                    trackBaseFrame = cursor;
                    submittedFrame = cursor;
                    lastKnownPosition = cursor;
                    trackStarted = false;
                }

                if (paused) {
                    synchronized (lock) {
                        while (paused && !cancelled && requestedSeek < 0) lock.wait();
                    }
                    continue;
                }

                long available = renderedFrames - cursor;
                if (available <= 0) {
                    if (renderingComplete) {
                        if (waitForDrain(track, cursor)) {
                            playbackComplete = true;
                            lastKnownPosition = finalFrames;
                            listener.onCompleted();
                            return;
                        }
                        continue;
                    }
                    buffering = true;
                    synchronized (lock) {
                        while (!cancelled && !failed && !renderingComplete
                                && renderedFrames <= cursor && requestedSeek < 0) {
                            lock.wait(100);
                        }
                    }
                    buffering = false;
                    continue;
                }

                int frames = (int) Math.min(BLOCK_FRAMES, available);
                long byteOffset = 44 + cursor * BYTES_PER_FRAME;
                if (input.getFilePointer() != byteOffset) input.seek(byteOffset);
                input.readFully(bytes, 0, frames * BYTES_PER_FRAME);
                int offset = 0;
                int remaining = frames * BYTES_PER_FRAME;
                while (remaining > 0 && !cancelled && !paused && requestedSeek < 0) {
                    int written = track.write(bytes, offset, remaining,
                            AudioTrack.WRITE_NON_BLOCKING);
                    if (written < 0) throw new IOException("AudioTrack write " + written);
                    if (written == 0) {
                        Thread.sleep(5);
                        continue;
                    }
                    offset += written;
                    remaining -= written;
                    int writtenFrames = written / BYTES_PER_FRAME;
                    cursor += writtenFrames;
                    submittedFrame = cursor;
                    if (!trackStarted) {
                        track.play();
                        trackStarted = true;
                        buffering = false;
                    }
                }
            }
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        } catch (Exception error) {
            if (!cancelled) {
                String message = error.getMessage() == null
                        ? error.getClass().getSimpleName() : error.getMessage();
                fail("音声の再生に失敗しました: " + message);
            }
        } finally {
            ready = false;
            audioTrack = null;
            releaseTrack(track);
            if (input != null) {
                try {
                    input.close();
                } catch (IOException ignored) {
                    // Nothing actionable during teardown.
                }
            }
        }
    }

    private boolean waitForDrain(AudioTrack track, long endFrame) throws InterruptedException {
        while (!cancelled && !failed) {
            if (requestedSeek >= 0) return false;
            if (paused) {
                synchronized (lock) {
                    while (paused && !cancelled && requestedSeek < 0) lock.wait();
                }
                continue;
            }
            long position = trackBaseFrame
                    + Integer.toUnsignedLong(track.getPlaybackHeadPosition());
            lastKnownPosition = Math.min(endFrame, position);
            if (position >= endFrame) return true;
            Thread.sleep(20);
        }
        return false;
    }

    private AudioTrack createAudioTrack() throws IOException {
        int minimum = AudioTrack.getMinBufferSize(SAMPLE_RATE,
                AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        if (minimum <= 0) throw new IOException("AudioTrack buffer " + minimum);
        int bufferBytes = Math.max(minimum, SAMPLE_RATE * BYTES_PER_FRAME / 2);
        AudioTrack track = new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build())
                .setAudioFormat(new AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setSampleRate(SAMPLE_RATE)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                        .build())
                .setTransferMode(AudioTrack.MODE_STREAM)
                .setBufferSizeInBytes(bufferBytes)
                .build();
        if (track.getState() != AudioTrack.STATE_INITIALIZED) {
            track.release();
            throw new IOException("AudioTrackを初期化できません");
        }
        track.setVolume(volume);
        return track;
    }

    private void fail(String message) {
        if (cancelled || failed) return;
        failed = true;
        synchronized (lock) {
            lock.notifyAll();
        }
        listener.onError(message);
    }

    private static void releaseTrack(AudioTrack track) {
        if (track == null) return;
        try {
            track.pause();
            track.flush();
            track.stop();
        } catch (IllegalStateException ignored) {
            // A partially initialized or already released track needs no stop.
        }
        track.release();
    }

    private static void writeWavHeader(RandomAccessFile output, long frames) throws IOException {
        long dataBytes = frames * BYTES_PER_FRAME;
        if (dataBytes > 0xffffffffL - 36) throw new IOException("曲が長すぎます");
        output.seek(0);
        output.writeBytes("RIFF");
        writeLittle32(output, 36 + dataBytes);
        output.writeBytes("WAVEfmt ");
        writeLittle32(output, 16);
        writeLittle16(output, 1);
        writeLittle16(output, CHANNELS);
        writeLittle32(output, SAMPLE_RATE);
        writeLittle32(output, SAMPLE_RATE * BYTES_PER_FRAME);
        writeLittle16(output, BYTES_PER_FRAME);
        writeLittle16(output, 16);
        output.writeBytes("data");
        writeLittle32(output, dataBytes);
    }

    private static void writeLittle16(RandomAccessFile output, long value) throws IOException {
        output.write((int) (value & 0xff));
        output.write((int) ((value >>> 8) & 0xff));
    }

    private static void writeLittle32(RandomAccessFile output, long value) throws IOException {
        output.write((int) (value & 0xff));
        output.write((int) ((value >>> 8) & 0xff));
        output.write((int) ((value >>> 16) & 0xff));
        output.write((int) ((value >>> 24) & 0xff));
    }
}
