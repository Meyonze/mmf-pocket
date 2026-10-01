package app.mmfpocket.player;

import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

/** Shared MMF input and WAV cache operations used by playback and batch conversion. */
final class MmfAudioRepository {
    // Bump whenever synthesis, timing or post-processing changes so an older
    // WAV cannot hide a renderer fix behind a valid content hash.
    private static final String RENDER_CACHE_VERSION = "r17";
    private static final int MAX_MMF_BYTES = 16 * 1024 * 1024;
    private static final long MAX_CACHE_BYTES = 512L * 1024 * 1024;
    private static final long STALE_PARTIAL_AGE_MS = 24L * 60 * 60 * 1000;
    static final String CACHE_DIR_NAME = "converted";

    private MmfAudioRepository() {}

    static PlaybackSource preparePlaybackSource(
            Context context, Uri uri, boolean phoneSpeakerMode) throws IOException {
        byte[] bytes = readMmf(context, uri);
        String formatLabel = NativeMmfRenderer.detectFormat(bytes);
        int motionPace = NativeMmfRenderer.detectMotionPace(bytes);
        File cacheDirectory = new File(context.getCacheDir(), CACHE_DIR_NAME);
        if (!cacheDirectory.isDirectory() && !cacheDirectory.mkdirs()) {
            throw new IOException(context.getString(R.string.cache_create_failed));
        }

        String key = RENDER_CACHE_VERSION + "-" + sha256(bytes);
        File cached = new File(cacheDirectory,
                key + (phoneSpeakerMode ? "-phone.wav" : ".wav"));
        if (cached.isFile() && cached.length() > 44) {
            //noinspection ResultOfMethodCallIgnored
            cached.setLastModified(System.currentTimeMillis());
            return new PlaybackSource(
                    bytes, cached, cached, null, phoneSpeakerMode, formatLabel,
                    motionPace, "");
        }

        File partial = new File(cacheDirectory, key + "-" + System.nanoTime() + ".part");
        return new PlaybackSource(
                bytes, null, cached, partial, phoneSpeakerMode, formatLabel,
                motionPace, "");
    }

    static RenderResult renderOrGetCached(
            Context context, Uri uri, boolean phoneSpeakerMode) throws IOException {
        PlaybackSource source = preparePlaybackSource(context, uri, phoneSpeakerMode);
        if (source.cached != null) return new RenderResult(source.cached, "", true);
        String error = NativeMmfRenderer.renderToWav(
                source.bytes, source.partial.getAbsolutePath(), phoneSpeakerMode);
        if (error == null) error = "";
        if (!error.isEmpty()) {
            //noinspection ResultOfMethodCallIgnored
            source.partial.delete();
            return new RenderResult(null, error, false);
        }
        Files.move(source.partial.toPath(), source.target.toPath(),
                StandardCopyOption.REPLACE_EXISTING);
        trimCache(source.target.getParentFile(), source.target);
        return new RenderResult(source.target, "", false);
    }

    static synchronized void trimCache(File cacheDirectory, File protectedFile) {
        File[] files = cacheDirectory.listFiles((directory, name) -> name.endsWith(".wav"));
        if (files == null) return;
        List<File> sorted = new ArrayList<>();
        long total = 0;
        for (File file : files) {
            sorted.add(file);
            total += file.length();
        }
        sorted.sort(Comparator.comparingLong(File::lastModified));
        for (File file : sorted) {
            if (total <= MAX_CACHE_BYTES) break;
            if (file.equals(protectedFile)) continue;
            long length = file.length();
            if (file.delete()) total -= length;
        }
    }

    static void cleanupStalePartials(Context context) {
        File cacheDirectory = new File(context.getCacheDir(), CACHE_DIR_NAME);
        File[] partials = cacheDirectory.listFiles((directory, name) -> name.endsWith(".part"));
        if (partials == null) return;
        long cutoff = System.currentTimeMillis() - STALE_PARTIAL_AGE_MS;
        for (File partial : partials) {
            if (partial.lastModified() >= cutoff) continue;
            //noinspection ResultOfMethodCallIgnored
            partial.delete();
        }
    }

    private static byte[] readMmf(Context context, Uri uri) throws IOException {
        ContentResolver resolver = context.getContentResolver();
        try (InputStream input = resolver.openInputStream(uri);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            if (input == null) throw new IOException(context.getString(R.string.file_open_failed));
            byte[] buffer = new byte[32 * 1024];
            int total = 0;
            int count;
            while ((count = input.read(buffer)) >= 0) {
                total += count;
                if (total > MAX_MMF_BYTES) {
                    throw new IOException(context.getString(R.string.file_too_large));
                }
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        }
    }

    private static String sha256(byte[] bytes) {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(bytes);
            StringBuilder result = new StringBuilder(digest.length * 2);
            for (byte value : digest) {
                result.append(String.format(Locale.ROOT, "%02x", value & 0xff));
            }
            return result.toString();
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException(impossible);
        }
    }

    static final class RenderResult {
        final File file;
        final String error;
        final boolean cacheHit;

        RenderResult(File file, String error, boolean cacheHit) {
            this.file = file;
            this.error = error;
            this.cacheHit = cacheHit;
        }
    }

    static final class PlaybackSource {
        final byte[] bytes;
        final File cached;
        final File target;
        final File partial;
        final boolean phoneSpeakerMode;
        final String formatLabel;
        final int motionPace;
        final String error;

        PlaybackSource(byte[] bytes, File cached, File target, File partial,
                       boolean phoneSpeakerMode, String formatLabel, int motionPace,
                       String error) {
            this.bytes = bytes;
            this.cached = cached;
            this.target = target;
            this.partial = partial;
            this.phoneSpeakerMode = phoneSpeakerMode;
            this.formatLabel = formatLabel;
            this.motionPace = motionPace;
            this.error = error;
        }

        static PlaybackSource error(String message) {
            return new PlaybackSource(null, null, null, null, false, "", 1, message);
        }
    }
}
