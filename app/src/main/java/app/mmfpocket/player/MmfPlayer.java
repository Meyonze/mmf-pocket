package app.mmfpocket.player;

import android.annotation.SuppressLint;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;

import androidx.media3.common.AudioAttributes;
import androidx.media3.common.C;
import androidx.media3.common.MediaItem;
import androidx.media3.common.MediaMetadata;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.common.SimpleBasePlayer;
import androidx.media3.common.util.UnstableApi;

import com.google.common.collect.ImmutableList;
import com.google.common.util.concurrent.Futures;
import com.google.common.util.concurrent.ListenableFuture;

import java.io.File;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ThreadLocalRandom;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.BooleanSupplier;

/** Media3 adapter that exposes the existing MMF engines as a single audio player. */
@UnstableApi
final class MmfPlayer extends SimpleBasePlayer {
    static final String EXTRA_PHONE_SPEAKER = "app.mmfpocket.extra.PHONE_SPEAKER";
    static final String EXTRA_FORMAT_LABEL = "app.mmfpocket.extra.FORMAT_LABEL";
    static final String EXTRA_MOTION_PACE = "app.mmfpocket.extra.MOTION_PACE";

    private final Context context;
    private final Handler mainHandler;
    private final ExecutorService preparationWorker = Executors.newSingleThreadExecutor();
    private final AudioManager audioManager;
    private final AudioFocusRequest audioFocusRequest;
    private final BooleanSupplier foregroundServiceReady;
    private final PowerManager.WakeLock wakeLock;
    private final BroadcastReceiver noisyReceiver;
    private final Player.Commands availableCommands;
    private final AudioAttributes media3AudioAttributes;

    private final List<MediaItem> playlist = new ArrayList<>();
    private final Set<String> playedMediaIds = new HashSet<>();
    private final ArrayDeque<Integer> playbackHistory = new ArrayDeque<>();
    private MediaItem currentItem;
    private int currentIndex = C.INDEX_UNSET;
    private MediaPlayer mediaPlayer;
    private ProgressivePlayback progressivePlayback;
    private File currentWav;
    private int generation;
    private int playbackState = Player.STATE_IDLE;
    private int playbackSuppressionReason = Player.PLAYBACK_SUPPRESSION_REASON_NONE;
    private int playWhenReadyReason = Player.PLAY_WHEN_READY_CHANGE_REASON_USER_REQUEST;
    private PlaybackException playerError;
    private boolean playWhenReady;
    private boolean prepared;
    private boolean hasAudioFocus;
    private boolean focusRequested;
    private int focusRequestGeneration;
    private boolean released;
    private boolean receiverRegistered;
    private long durationMs = C.TIME_UNSET;
    private long startPositionMs;
    private long pendingDiscontinuityMs = C.TIME_UNSET;
    private int pendingDiscontinuityReason = Player.DISCONTINUITY_REASON_SEEK;
    private boolean continuousPlayback;
    private boolean randomPlayback;
    private float volume = 1f;

    MmfPlayer(Context context, Looper looper, BooleanSupplier foregroundServiceReady) {
        super(looper);
        this.context = context.getApplicationContext();
        this.foregroundServiceReady = foregroundServiceReady;
        mainHandler = new Handler(looper);
        audioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        PowerManager powerManager =
                (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        wakeLock = powerManager.newWakeLock(
                PowerManager.PARTIAL_WAKE_LOCK, "MMFPocket:Playback");
        wakeLock.setReferenceCounted(false);

        android.media.AudioAttributes platformAudioAttributes =
                new android.media.AudioAttributes.Builder()
                        .setUsage(android.media.AudioAttributes.USAGE_MEDIA)
                        .setContentType(android.media.AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build();
        audioFocusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(platformAudioAttributes)
                .setAcceptsDelayedFocusGain(true)
                .setWillPauseWhenDucked(true)
                .setOnAudioFocusChangeListener(this::onAudioFocusChanged, mainHandler)
                .build();
        media3AudioAttributes = new AudioAttributes.Builder()
                .setUsage(C.USAGE_MEDIA)
                .setContentType(C.AUDIO_CONTENT_TYPE_MUSIC)
                .build();

        availableCommands = new Player.Commands.Builder()
                .addAll(
                        Player.COMMAND_PLAY_PAUSE,
                        Player.COMMAND_PREPARE,
                        Player.COMMAND_STOP,
                        Player.COMMAND_SEEK_TO_DEFAULT_POSITION,
                        Player.COMMAND_SEEK_IN_CURRENT_MEDIA_ITEM,
                        Player.COMMAND_SEEK_TO_MEDIA_ITEM,
                        Player.COMMAND_SEEK_TO_NEXT,
                        Player.COMMAND_SEEK_TO_NEXT_MEDIA_ITEM,
                        Player.COMMAND_SEEK_TO_PREVIOUS,
                        Player.COMMAND_SEEK_TO_PREVIOUS_MEDIA_ITEM,
                        Player.COMMAND_GET_CURRENT_MEDIA_ITEM,
                        Player.COMMAND_GET_TIMELINE,
                        Player.COMMAND_GET_METADATA,
                        Player.COMMAND_SET_MEDIA_ITEM,
                        Player.COMMAND_GET_AUDIO_ATTRIBUTES,
                        Player.COMMAND_GET_VOLUME,
                        Player.COMMAND_SET_VOLUME)
                .build();

        noisyReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context ignored, Intent intent) {
                if (AudioManager.ACTION_AUDIO_BECOMING_NOISY.equals(intent.getAction())) {
                    pauseForUser(Player.PLAY_WHEN_READY_CHANGE_REASON_AUDIO_BECOMING_NOISY);
                }
            }
        };
        IntentFilter filter = new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            this.context.registerReceiver(noisyReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            this.context.registerReceiver(noisyReceiver, filter);
        }
        receiverRegistered = true;
    }

    @Override
    protected State getState() {
        State.Builder builder = new State.Builder()
                .setAvailableCommands(availableCommands)
                .setPlayWhenReady(playWhenReady, playWhenReadyReason)
                .setPlaybackState(playbackState)
                .setPlaybackSuppressionReason(playbackSuppressionReason)
                .setPlayerError(playerError)
                .setIsLoading(playbackState == Player.STATE_BUFFERING)
                .setAudioAttributes(media3AudioAttributes)
                .setVolume(volume);

        if (currentItem != null && currentIndex != C.INDEX_UNSET) {
            ImmutableList.Builder<MediaItemData> playlistBuilder = ImmutableList.builder();
            for (int index = 0; index < playlist.size(); index++) {
                MediaItem item = playlist.get(index);
                boolean isCurrent = index == currentIndex;
                long itemDurationUs = isCurrent && durationMs != C.TIME_UNSET
                        ? durationMs * 1000L : C.TIME_UNSET;
                playlistBuilder.add(new MediaItemData.Builder(item.mediaId + "#" + index)
                        .setMediaItem(item)
                        .setMediaMetadata(item.mediaMetadata)
                        .setDurationUs(itemDurationUs)
                        .setIsSeekable(isCurrent && prepared)
                        .setIsPlaceholder(false)
                        .build());
            }
            builder.setPlaylist(playlistBuilder.build())
                    .setCurrentMediaItemIndex(currentIndex)
                    .setContentPositionMs((PositionSupplier) this::getBackendPositionMs)
                    .setContentBufferedPositionMs((PositionSupplier) this::getBackendBufferedMs)
                    .setTotalBufferedDurationMs((PositionSupplier) () -> Math.max(
                            0, getBackendBufferedMs() - getBackendPositionMs()));
        }
        if (pendingDiscontinuityMs != C.TIME_UNSET) {
            builder.setPositionDiscontinuity(
                    pendingDiscontinuityReason, pendingDiscontinuityMs);
            pendingDiscontinuityMs = C.TIME_UNSET;
        }
        return builder.build();
    }

    @Override
    protected ListenableFuture<?> handleSetMediaItems(
            List<MediaItem> mediaItems, int startIndex, long startPositionMs) {
        generation++;
        releaseBackend();
        playerError = null;
        prepared = false;
        playbackState = Player.STATE_IDLE;
        playbackSuppressionReason = Player.PLAYBACK_SUPPRESSION_REASON_NONE;
        durationMs = C.TIME_UNSET;
        currentWav = null;
        playlist.clear();
        playlist.addAll(mediaItems);
        playedMediaIds.clear();
        playbackHistory.clear();
        currentIndex = mediaItems.isEmpty() ? C.INDEX_UNSET
                : Math.max(0, Math.min(startIndex, mediaItems.size() - 1));
        currentItem = currentIndex == C.INDEX_UNSET ? null : playlist.get(currentIndex);
        if (currentItem == null) {
            playWhenReady = false;
            abandonAudioFocus();
            setWakeLockHeld(false);
        }
        this.startPositionMs = startPositionMs == C.TIME_UNSET ? 0 : Math.max(0, startPositionMs);
        invalidateState();
        return Futures.immediateVoidFuture();
    }

    @Override
    protected ListenableFuture<?> handlePrepare() {
        if (currentItem == null || playbackState == Player.STATE_BUFFERING
                || playbackState == Player.STATE_READY) {
            return Futures.immediateVoidFuture();
        }
        Uri uri = currentItem.localConfiguration == null
                ? null : currentItem.localConfiguration.uri;
        if (uri == null) {
            fail("MMFファイルを開けません", PlaybackException.ERROR_CODE_IO_FILE_NOT_FOUND);
            return Futures.immediateVoidFuture();
        }

        playbackState = Player.STATE_BUFFERING;
        playerError = null;
        prepared = false;
        invalidateState();
        int requestGeneration = ++generation;
        boolean phoneSpeakerMode = currentItem.mediaMetadata.extras != null
                && currentItem.mediaMetadata.extras.getBoolean(EXTRA_PHONE_SPEAKER, false);
        preparationWorker.execute(() -> {
            MmfAudioRepository.PlaybackSource source;
            try {
                source = MmfAudioRepository.preparePlaybackSource(
                        context, uri, phoneSpeakerMode);
            } catch (Exception error) {
                String message = error.getMessage() == null
                        ? error.getClass().getSimpleName() : error.getMessage();
                source = MmfAudioRepository.PlaybackSource.error(message);
            }
            MmfAudioRepository.PlaybackSource result = source;
            mainHandler.post(() -> finishPreparation(requestGeneration, result));
        });
        return Futures.immediateVoidFuture();
    }

    @Override
    protected ListenableFuture<?> handleSetPlayWhenReady(boolean value) {
        playWhenReady = value;
        playWhenReadyReason = Player.PLAY_WHEN_READY_CHANGE_REASON_USER_REQUEST;
        if (!value) {
            playbackSuppressionReason = Player.PLAYBACK_SUPPRESSION_REASON_NONE;
            pauseBackend();
            abandonAudioFocus();
            setWakeLockHeld(false);
            invalidateState();
            return Futures.immediateVoidFuture();
        }
        if (currentItem == null) {
            playWhenReady = false;
            invalidateState();
            return Futures.immediateVoidFuture();
        }

        setWakeLockHeld(true);
        if (playbackState == Player.STATE_ENDED) {
            restartCompletedPlayback(0);
        } else if (playbackState == Player.STATE_IDLE && currentItem != null) {
            handlePrepare();
        }
        invalidateState();
        // On Android 15 the service must actually be foreground before a
        // background audio-focus request is eligible.
        int focusGeneration = ++focusRequestGeneration;
        mainHandler.post(() -> requestFocusWhenEligible(focusGeneration, 0));
        return Futures.immediateVoidFuture();
    }

    @Override
    protected ListenableFuture<?> handleSeek(int mediaItemIndex, long positionMs, int seekCommand) {
        if (currentItem == null) return Futures.immediateVoidFuture();
        boolean seekToNext = seekCommand == Player.COMMAND_SEEK_TO_NEXT
                || seekCommand == Player.COMMAND_SEEK_TO_NEXT_MEDIA_ITEM;
        if (seekToNext) {
            skipToNext();
            return Futures.immediateVoidFuture();
        }
        boolean seekToPrevious = seekCommand == Player.COMMAND_SEEK_TO_PREVIOUS
                || seekCommand == Player.COMMAND_SEEK_TO_PREVIOUS_MEDIA_ITEM;
        if (seekToPrevious) {
            skipToPrevious();
            return Futures.immediateVoidFuture();
        }
        int requestedIndex = mediaItemIndex == C.INDEX_UNSET ? currentIndex : mediaItemIndex;
        if (requestedIndex < 0 || requestedIndex >= playlist.size()) {
            return Futures.immediateVoidFuture();
        }
        long requested = positionMs == C.TIME_UNSET ? 0 : Math.max(0, positionMs);
        if (requestedIndex != currentIndex) {
            playedMediaIds.add(currentItem.mediaId);
            transitionToItem(requestedIndex, requested, Player.DISCONTINUITY_REASON_SEEK,
                    playWhenReady);
            return Futures.immediateVoidFuture();
        }
        if (durationMs != C.TIME_UNSET) requested = Math.min(requested, durationMs);
        if (playbackState == Player.STATE_ENDED) {
            restartCompletedPlayback(requested);
            pendingDiscontinuityMs = requested;
            invalidateState();
            return Futures.immediateVoidFuture();
        }
        long actual = seekBackend(requested);
        startPositionMs = actual;
        pendingDiscontinuityMs = actual;
        if (playbackState == Player.STATE_ENDED) playbackState = Player.STATE_READY;
        invalidateState();
        return Futures.immediateVoidFuture();
    }

    @Override
    protected ListenableFuture<?> handleStop() {
        generation++;
        playWhenReady = false;
        playWhenReadyReason = Player.PLAY_WHEN_READY_CHANGE_REASON_USER_REQUEST;
        playbackSuppressionReason = Player.PLAYBACK_SUPPRESSION_REASON_NONE;
        playbackState = Player.STATE_IDLE;
        prepared = false;
        startPositionMs = 0;
        pendingDiscontinuityMs = 0;
        releaseBackend();
        abandonAudioFocus();
        setWakeLockHeld(false);
        invalidateState();
        return Futures.immediateVoidFuture();
    }

    @Override
    protected ListenableFuture<?> handleRelease() {
        if (released) return Futures.immediateVoidFuture();
        released = true;
        generation++;
        releaseBackend();
        abandonAudioFocus();
        setWakeLockHeld(false);
        preparationWorker.shutdownNow();
        if (receiverRegistered) {
            receiverRegistered = false;
            try {
                context.unregisterReceiver(noisyReceiver);
            } catch (IllegalArgumentException ignored) {
                // Already unregistered by process teardown.
            }
        }
        currentItem = null;
        playlist.clear();
        currentIndex = C.INDEX_UNSET;
        playbackState = Player.STATE_IDLE;
        return Futures.immediateVoidFuture();
    }

    @Override
    protected ListenableFuture<?> handleSetVolume(float value) {
        volume = Math.max(0f, Math.min(1f, value));
        if (mediaPlayer != null) mediaPlayer.setVolume(volume, volume);
        if (progressivePlayback != null) progressivePlayback.setVolume(volume);
        invalidateState();
        return Futures.immediateVoidFuture();
    }

    private void finishPreparation(int requestGeneration,
                                   MmfAudioRepository.PlaybackSource source) {
        if (released || requestGeneration != generation) {
            if (source.partial != null) {
                //noinspection ResultOfMethodCallIgnored
                source.partial.delete();
            }
            return;
        }
        if (!source.error.isEmpty()) {
            fail(source.error, PlaybackException.ERROR_CODE_IO_UNSPECIFIED);
            return;
        }
        updateFormatMetadata(source.formatLabel, source.motionPace);
        if (source.cached != null) startMediaPlayer(source.cached, requestGeneration);
        else startProgressivePlayback(source, requestGeneration);
    }

    private void startProgressivePlayback(
            MmfAudioRepository.PlaybackSource source, int requestGeneration) {
        ProgressivePlayback playback = new ProgressivePlayback(
                source.bytes, source.phoneSpeakerMode, source.partial, source.target,
                new ProgressivePlayback.Listener() {
                    @Override
                    public void onReady() {
                        mainHandler.post(() -> {
                            if (requestGeneration != generation || released) return;
                            prepared = true;
                            playbackState = Player.STATE_READY;
                            if (progressivePlayback == null) return;
                            durationMs = framesToMs(progressivePlayback.getTimelineFrames());
                            if (shouldOutputAudio()) progressivePlayback.resume();
                            else progressivePlayback.pause();
                            if (startPositionMs > 0) seekBackend(startPositionMs);
                            invalidateState();
                        });
                    }

                    @Override
                    public void onCacheReady(File cached) {
                        MmfAudioRepository.trimCache(cached.getParentFile(), cached);
                        mainHandler.post(() -> {
                            if (requestGeneration != generation || released) return;
                            currentWav = cached;
                            if (progressivePlayback == null) return;
                            durationMs = framesToMs(progressivePlayback.getTimelineFrames());
                            invalidateState();
                        });
                    }

                    @Override
                    public void onCompleted() {
                        mainHandler.post(() -> {
                            if (requestGeneration != generation || released) return;
                            onCurrentItemCompleted();
                        });
                    }

                    @Override
                    public void onError(String message) {
                        mainHandler.post(() -> {
                            if (requestGeneration != generation || released) return;
                            fail(message, PlaybackException.ERROR_CODE_AUDIO_TRACK_WRITE_FAILED);
                        });
                    }
                });
        progressivePlayback = playback;
        playback.setVolume(volume);
        if (!shouldOutputAudio()) playback.pause();
        playback.start();
        invalidateState();
    }

    private void startMediaPlayer(File wav, int requestGeneration) {
        releaseBackend();
        currentWav = wav;
        MediaPlayer player = new MediaPlayer();
        mediaPlayer = player;
        try {
            player.setAudioAttributes(new android.media.AudioAttributes.Builder()
                    .setUsage(android.media.AudioAttributes.USAGE_MEDIA)
                    .setContentType(android.media.AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build());
            player.setVolume(volume, volume);
            player.setDataSource(wav.getAbsolutePath());
            player.setOnPreparedListener(preparedPlayer -> {
                if (requestGeneration != generation || preparedPlayer != mediaPlayer) return;
                prepared = true;
                durationMs = preparedPlayer.getDuration();
                if (startPositionMs > 0) {
                    preparedPlayer.seekTo((int) Math.min(startPositionMs, durationMs));
                }
                playbackState = Player.STATE_READY;
                if (shouldOutputAudio()) preparedPlayer.start();
                invalidateState();
            });
            player.setOnCompletionListener(completedPlayer -> {
                if (requestGeneration != generation || completedPlayer != mediaPlayer) return;
                onCurrentItemCompleted();
            });
            player.setOnErrorListener((failedPlayer, what, extra) -> {
                if (requestGeneration == generation && failedPlayer == mediaPlayer) {
                    fail("音声の再生に失敗しました",
                            PlaybackException.ERROR_CODE_AUDIO_TRACK_WRITE_FAILED);
                }
                return true;
            });
            player.prepareAsync();
        } catch (Exception error) {
            String message = error.getMessage() == null
                    ? error.getClass().getSimpleName() : error.getMessage();
            fail("音声を開けません: " + message, PlaybackException.ERROR_CODE_IO_UNSPECIFIED);
        }
    }

    private void restartCompletedPlayback(long positionMs) {
        startPositionMs = Math.max(0, positionMs);
        if (currentWav != null && currentWav.isFile()) {
            int requestGeneration = ++generation;
            playbackState = Player.STATE_BUFFERING;
            prepared = false;
            startMediaPlayer(currentWav, requestGeneration);
            return;
        }
        playbackState = Player.STATE_IDLE;
        prepared = false;
        handlePrepare();
    }

    void startFolderQueue(
            List<MediaItem> items, int startIndex, boolean continuous, boolean random) {
        continuousPlayback = continuous;
        randomPlayback = random;
        handleSetMediaItems(items, startIndex, 0);
        if (currentItem == null) return;
        handlePrepare();
        handleSetPlayWhenReady(true);
    }

    void setContinuousPlayback(boolean enabled) {
        continuousPlayback = enabled;
    }

    void setRandomPlayback(boolean enabled) {
        randomPlayback = enabled;
    }

    void skipToNext() {
        if (currentItem == null) return;
        playedMediaIds.add(currentItem.mediaId);
        int nextIndex = randomPlayback
                ? chooseRandomUnplayedIndex() : chooseNextSequentialIndex();
        if (nextIndex == C.INDEX_UNSET) return;
        playbackHistory.addLast(currentIndex);
        transitionToItem(nextIndex, 0, Player.DISCONTINUITY_REASON_SEEK, playWhenReady);
    }

    void skipToPrevious() {
        if (currentItem == null) return;
        int previousIndex = playbackHistory.isEmpty()
                ? currentIndex - 1 : playbackHistory.removeLast();
        if (previousIndex < 0 || previousIndex >= playlist.size()) return;
        transitionToItem(previousIndex, 0, Player.DISCONTINUITY_REASON_SEEK, playWhenReady);
    }

    void setPhoneSoundMode(boolean enabled) {
        if (playlist.isEmpty()) return;
        generation++;
        releaseBackend();
        playWhenReady = false;
        playWhenReadyReason = Player.PLAY_WHEN_READY_CHANGE_REASON_USER_REQUEST;
        playbackSuppressionReason = Player.PLAYBACK_SUPPRESSION_REASON_NONE;
        playbackState = Player.STATE_IDLE;
        playerError = null;
        prepared = false;
        durationMs = C.TIME_UNSET;
        currentWav = null;
        startPositionMs = 0;
        pendingDiscontinuityMs = 0;
        pendingDiscontinuityReason = Player.DISCONTINUITY_REASON_SEEK;
        for (int index = 0; index < playlist.size(); index++) {
            MediaItem item = playlist.get(index);
            Bundle extras = item.mediaMetadata.extras == null
                    ? new Bundle() : new Bundle(item.mediaMetadata.extras);
            extras.putBoolean(EXTRA_PHONE_SPEAKER, enabled);
            MediaMetadata metadata = new MediaMetadata.Builder()
                    .populate(item.mediaMetadata)
                    .setExtras(extras)
                    .build();
            playlist.set(index, item.buildUpon().setMediaMetadata(metadata).build());
        }
        currentItem = playlist.get(currentIndex);
        abandonAudioFocus();
        setWakeLockHeld(false);
        invalidateState();
    }

    private void onCurrentItemCompleted() {
        playedMediaIds.add(currentItem.mediaId);
        int nextIndex = randomPlayback
                ? chooseRandomUnplayedIndex() : chooseNextSequentialIndex();
        if (continuousPlayback && playWhenReady && nextIndex != C.INDEX_UNSET) {
            playbackHistory.addLast(currentIndex);
            transitionToItem(nextIndex, 0,
                    Player.DISCONTINUITY_REASON_AUTO_TRANSITION, true);
            return;
        }
        playbackState = Player.STATE_ENDED;
        playWhenReady = false;
        playWhenReadyReason = Player.PLAY_WHEN_READY_CHANGE_REASON_END_OF_MEDIA_ITEM;
        abandonAudioFocus();
        setWakeLockHeld(false);
        invalidateState();
    }

    private int chooseRandomUnplayedIndex() {
        int candidateCount = 0;
        for (int index = 0; index < playlist.size(); index++) {
            if (!playedMediaIds.contains(playlist.get(index).mediaId)
                    && index != currentIndex) candidateCount++;
        }
        if (candidateCount == 0) return C.INDEX_UNSET;
        int selected = ThreadLocalRandom.current().nextInt(candidateCount);
        for (int index = 0; index < playlist.size(); index++) {
            if (playedMediaIds.contains(playlist.get(index).mediaId)
                    || index == currentIndex) continue;
            if (selected-- == 0) return index;
        }
        return C.INDEX_UNSET;
    }

    private int chooseNextSequentialIndex() {
        for (int index = currentIndex + 1; index < playlist.size(); index++) {
            if (!playedMediaIds.contains(playlist.get(index).mediaId)) return index;
        }
        return C.INDEX_UNSET;
    }

    private void transitionToItem(int index, long positionMs, int reason, boolean prepareNext) {
        generation++;
        releaseBackend();
        currentIndex = index;
        currentItem = playlist.get(index);
        playerError = null;
        prepared = false;
        playbackState = prepareNext ? Player.STATE_BUFFERING : Player.STATE_IDLE;
        durationMs = C.TIME_UNSET;
        currentWav = null;
        startPositionMs = Math.max(0, positionMs);
        pendingDiscontinuityMs = startPositionMs;
        pendingDiscontinuityReason = reason;
        invalidateState();
        if (prepareNext) {
            playbackState = Player.STATE_IDLE;
            handlePrepare();
        }
    }

    private void requestFocusWhenEligible(int requestGeneration, int checkCount) {
        if (!playWhenReady || released || requestGeneration != focusRequestGeneration) return;
        if (Build.VERSION.SDK_INT >= 35 && !foregroundServiceReady.getAsBoolean()) {
            if (checkCount < 100) {
                mainHandler.postDelayed(
                        () -> requestFocusWhenEligible(requestGeneration, checkCount + 1), 50);
            } else {
                rejectFocusRequest();
            }
            return;
        }

        int result = audioManager.requestAudioFocus(audioFocusRequest);
        if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            setWakeLockHeld(true);
            focusRequested = true;
            hasAudioFocus = true;
            playbackSuppressionReason = Player.PLAYBACK_SUPPRESSION_REASON_NONE;
            resumeBackend();
        } else if (result == AudioManager.AUDIOFOCUS_REQUEST_DELAYED) {
            focusRequested = true;
            hasAudioFocus = false;
            playbackSuppressionReason =
                    Player.PLAYBACK_SUPPRESSION_REASON_TRANSIENT_AUDIO_FOCUS_LOSS;
            pauseBackend();
            setWakeLockHeld(false);
        } else {
            focusRequested = false;
            hasAudioFocus = false;
            pauseBackend();
            rejectFocusRequest();
        }
        invalidateState();
    }

    private void rejectFocusRequest() {
        playWhenReady = false;
        playWhenReadyReason = Player.PLAY_WHEN_READY_CHANGE_REASON_AUDIO_FOCUS_LOSS;
        playbackSuppressionReason = Player.PLAYBACK_SUPPRESSION_REASON_NONE;
        pauseBackend();
        setWakeLockHeld(false);
        invalidateState();
    }

    private void onAudioFocusChanged(int change) {
        if (released) return;
        switch (change) {
            case AudioManager.AUDIOFOCUS_GAIN:
                if (!playWhenReady) {
                    audioManager.abandonAudioFocusRequest(audioFocusRequest);
                    focusRequested = false;
                    hasAudioFocus = false;
                    return;
                }
                setWakeLockHeld(playWhenReady);
                focusRequested = true;
                hasAudioFocus = true;
                playbackSuppressionReason = Player.PLAYBACK_SUPPRESSION_REASON_NONE;
                if (playWhenReady) resumeBackend();
                invalidateState();
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:
                hasAudioFocus = false;
                playbackSuppressionReason =
                        Player.PLAYBACK_SUPPRESSION_REASON_TRANSIENT_AUDIO_FOCUS_LOSS;
                pauseBackend();
                setWakeLockHeld(false);
                invalidateState();
                break;
            case AudioManager.AUDIOFOCUS_LOSS:
                focusRequestGeneration++;
                focusRequested = false;
                hasAudioFocus = false;
                playWhenReady = false;
                playWhenReadyReason = Player.PLAY_WHEN_READY_CHANGE_REASON_AUDIO_FOCUS_LOSS;
                playbackSuppressionReason = Player.PLAYBACK_SUPPRESSION_REASON_NONE;
                pauseBackend();
                setWakeLockHeld(false);
                invalidateState();
                break;
            default:
                break;
        }
    }

    private void pauseForUser(int reason) {
        playWhenReady = false;
        playWhenReadyReason = reason;
        playbackSuppressionReason = Player.PLAYBACK_SUPPRESSION_REASON_NONE;
        pauseBackend();
        abandonAudioFocus();
        setWakeLockHeld(false);
        invalidateState();
    }

    private boolean shouldOutputAudio() {
        return playWhenReady && hasAudioFocus
                && playbackSuppressionReason == Player.PLAYBACK_SUPPRESSION_REASON_NONE;
    }

    private void pauseBackend() {
        if (progressivePlayback != null) progressivePlayback.pause();
        if (mediaPlayer != null && mediaPlayer.isPlaying()) mediaPlayer.pause();
    }

    private void resumeBackend() {
        if (!prepared || playbackState != Player.STATE_READY) return;
        if (progressivePlayback != null) progressivePlayback.resume();
        if (mediaPlayer != null && !mediaPlayer.isPlaying()) mediaPlayer.start();
    }

    private long seekBackend(long requestedMs) {
        if (progressivePlayback != null) {
            long requestedFrames = requestedMs * ProgressivePlayback.SAMPLE_RATE / 1000L;
            return framesToMs(progressivePlayback.seekTo(requestedFrames));
        }
        if (mediaPlayer != null && prepared) {
            long clamped = durationMs == C.TIME_UNSET
                    ? requestedMs : Math.min(requestedMs, durationMs);
            mediaPlayer.seekTo((int) Math.min(Integer.MAX_VALUE, clamped));
            return clamped;
        }
        startPositionMs = requestedMs;
        return requestedMs;
    }

    private long getBackendPositionMs() {
        if (progressivePlayback != null) {
            return framesToMs(progressivePlayback.getPositionFrames());
        }
        if (mediaPlayer != null && prepared) {
            try {
                return mediaPlayer.getCurrentPosition();
            } catch (IllegalStateException ignored) {
                return 0;
            }
        }
        return Math.max(0, startPositionMs);
    }

    private long getBackendBufferedMs() {
        if (progressivePlayback != null) {
            return framesToMs(progressivePlayback.getRenderedFrames());
        }
        if (mediaPlayer != null && prepared && durationMs != C.TIME_UNSET) return durationMs;
        return getBackendPositionMs();
    }

    private long framesToMs(long frames) {
        return Math.max(0, frames * 1000L / ProgressivePlayback.SAMPLE_RATE);
    }

    private void updateFormatMetadata(String formatLabel, int motionPace) {
        if (currentItem == null) return;
        Bundle extras = currentItem.mediaMetadata.extras == null
                ? new Bundle() : new Bundle(currentItem.mediaMetadata.extras);
        extras.putString(EXTRA_FORMAT_LABEL, formatLabel);
        extras.putInt(EXTRA_MOTION_PACE, motionPace);
        MediaMetadata metadata = new MediaMetadata.Builder()
                .populate(currentItem.mediaMetadata)
                .setSubtitle(formatLabel)
                .setExtras(extras)
                .build();
        currentItem = currentItem.buildUpon().setMediaMetadata(metadata).build();
        playlist.set(currentIndex, currentItem);
        invalidateState();
    }

    private void fail(String message, int errorCode) {
        generation++;
        releaseBackend();
        prepared = false;
        playWhenReady = false;
        playbackSuppressionReason = Player.PLAYBACK_SUPPRESSION_REASON_NONE;
        playbackState = Player.STATE_IDLE;
        playerError = new PlaybackException(message, null, errorCode);
        abandonAudioFocus();
        setWakeLockHeld(false);
        invalidateState();
    }

    private void releaseBackend() {
        prepared = false;
        if (progressivePlayback != null) {
            progressivePlayback.cancel();
            progressivePlayback = null;
        }
        if (mediaPlayer != null) {
            mediaPlayer.setOnPreparedListener(null);
            mediaPlayer.setOnCompletionListener(null);
            mediaPlayer.setOnErrorListener(null);
            mediaPlayer.release();
            mediaPlayer = null;
        }
    }

    private void abandonAudioFocus() {
        focusRequestGeneration++;
        if (!focusRequested && !hasAudioFocus) return;
        audioManager.abandonAudioFocusRequest(audioFocusRequest);
        focusRequested = false;
        hasAudioFocus = false;
    }

    @SuppressLint("WakelockTimeout")
    private void setWakeLockHeld(boolean held) {
        if (held) {
            if (!wakeLock.isHeld()) wakeLock.acquire();
        } else if (wakeLock.isHeld()) {
            wakeLock.release();
        }
    }
}
