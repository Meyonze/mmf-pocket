package app.mmfpocket.player;

import android.app.PendingIntent;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;

import androidx.media3.common.MediaItem;
import androidx.media3.common.MediaMetadata;
import androidx.media3.common.Player;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.session.CommandButton;
import androidx.media3.session.MediaSession;
import androidx.media3.session.MediaSessionService;
import androidx.media3.session.SessionCommand;
import androidx.media3.session.SessionCommands;
import androidx.media3.session.SessionResult;

import com.google.common.util.concurrent.Futures;
import com.google.common.util.concurrent.ListenableFuture;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Owns MMF playback independently from the Activity lifecycle. */
@UnstableApi
public final class PlaybackService extends MediaSessionService {
    static final SessionCommand COMMAND_START_FOLDER_QUEUE = new SessionCommand(
            "app.mmfpocket.command.START_FOLDER_QUEUE", Bundle.EMPTY);
    static final SessionCommand COMMAND_SET_CONTINUOUS = new SessionCommand(
            "app.mmfpocket.command.SET_CONTINUOUS", Bundle.EMPTY);
    static final SessionCommand COMMAND_SET_RANDOM = new SessionCommand(
            "app.mmfpocket.command.SET_RANDOM", Bundle.EMPTY);
    static final SessionCommand COMMAND_SET_PHONE_SOUND = new SessionCommand(
            "app.mmfpocket.command.SET_PHONE_SOUND", Bundle.EMPTY);
    static final SessionCommand COMMAND_SKIP_PREVIOUS = new SessionCommand(
            "app.mmfpocket.command.SKIP_PREVIOUS", Bundle.EMPTY);
    static final SessionCommand COMMAND_SKIP_NEXT = new SessionCommand(
            "app.mmfpocket.command.SKIP_NEXT", Bundle.EMPTY);
    static final String ARG_URIS = "uris";
    static final String ARG_NAMES = "names";
    static final String ARG_START_INDEX = "start_index";
    static final String ARG_ENABLED = "enabled";
    static final String ARG_PHONE_SOUND = "phone_sound";
    static final String ARG_RANDOM = "random";

    private MmfPlayer player;
    private MediaSession mediaSession;

    @Override
    public void onCreate() {
        super.onCreate();
        MmfAudioRepository.cleanupStalePartials(this);
        player = new MmfPlayer(this, getMainLooper(), this::isPlaybackOngoing);

        Intent activityIntent = new Intent(this, MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent sessionActivity = PendingIntent.getActivity(
                this, 0, activityIntent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        CommandButton stopButton = new CommandButton.Builder(CommandButton.ICON_STOP)
                .setPlayerCommand(Player.COMMAND_STOP)
                .setDisplayName(getString(R.string.stop_description))
                .setSlots(
                        CommandButton.SLOT_FORWARD_SECONDARY,
                        CommandButton.SLOT_OVERFLOW)
                .build();
        mediaSession = new MediaSession.Builder(this, player)
                .setSessionActivity(sessionActivity)
                .setShowPlayButtonIfPlaybackIsSuppressed(true)
                .setMediaButtonPreferences(Collections.singletonList(stopButton))
                .setCallback(new MediaSession.Callback() {
                    @Override
                    public MediaSession.ConnectionResult onConnect(
                            MediaSession session, MediaSession.ControllerInfo controller) {
                        MediaSession.ConnectionResult.AcceptedResultBuilder result =
                                new MediaSession.ConnectionResult.AcceptedResultBuilder(session);
                        if (getPackageName().equals(controller.getPackageName())) {
                            SessionCommands commands = MediaSession.ConnectionResult
                                    .DEFAULT_SESSION_COMMANDS.buildUpon()
                                    .add(COMMAND_START_FOLDER_QUEUE)
                                    .add(COMMAND_SET_CONTINUOUS)
                                    .add(COMMAND_SET_RANDOM)
                                    .add(COMMAND_SET_PHONE_SOUND)
                                    .add(COMMAND_SKIP_PREVIOUS)
                                    .add(COMMAND_SKIP_NEXT)
                                    .build();
                            result.setAvailableSessionCommands(commands);
                        }
                        return result.build();
                    }

                    @Override
                    public ListenableFuture<SessionResult> onCustomCommand(
                            MediaSession session,
                            MediaSession.ControllerInfo controller,
                            SessionCommand command,
                            Bundle args) {
                        if (!getPackageName().equals(controller.getPackageName())) {
                            return result(SessionResult.RESULT_ERROR_PERMISSION_DENIED);
                        }
                        if (COMMAND_SET_CONTINUOUS.customAction.equals(command.customAction)) {
                            player.setContinuousPlayback(args.getBoolean(ARG_ENABLED, false));
                            return result(SessionResult.RESULT_SUCCESS);
                        }
                        if (COMMAND_SET_RANDOM.customAction.equals(command.customAction)) {
                            player.setRandomPlayback(args.getBoolean(ARG_ENABLED, false));
                            return result(SessionResult.RESULT_SUCCESS);
                        }
                        if (COMMAND_SET_PHONE_SOUND.customAction.equals(command.customAction)) {
                            player.setPhoneSoundMode(args.getBoolean(ARG_ENABLED, false));
                            return result(SessionResult.RESULT_SUCCESS);
                        }
                        if (COMMAND_SKIP_PREVIOUS.customAction.equals(command.customAction)) {
                            player.skipToPrevious();
                            return result(SessionResult.RESULT_SUCCESS);
                        }
                        if (COMMAND_SKIP_NEXT.customAction.equals(command.customAction)) {
                            player.skipToNext();
                            return result(SessionResult.RESULT_SUCCESS);
                        }
                        if (COMMAND_START_FOLDER_QUEUE.customAction.equals(command.customAction)) {
                            ArrayList<String> uris = args.getStringArrayList(ARG_URIS);
                            ArrayList<String> names = args.getStringArrayList(ARG_NAMES);
                            if (uris == null || names == null || uris.isEmpty()
                                    || uris.size() != names.size()) {
                                return result(SessionResult.RESULT_ERROR_BAD_VALUE);
                            }
                            boolean phoneSound = args.getBoolean(ARG_PHONE_SOUND, false);
                            List<MediaItem> items = new ArrayList<>(uris.size());
                            for (int index = 0; index < uris.size(); index++) {
                                String uri = uris.get(index);
                                String name = names.get(index);
                                if (uri == null || uri.isEmpty() || name == null) {
                                    return result(SessionResult.RESULT_ERROR_BAD_VALUE);
                                }
                                Bundle extras = new Bundle();
                                extras.putBoolean(MmfPlayer.EXTRA_PHONE_SPEAKER, phoneSound);
                                MediaMetadata metadata = new MediaMetadata.Builder()
                                        .setTitle(name)
                                        .setDisplayTitle(name)
                                        .setExtras(extras)
                                        .build();
                                items.add(new MediaItem.Builder()
                                        .setMediaId(uri)
                                        .setUri(Uri.parse(uri))
                                        .setMimeType("audio/x-smaf")
                                        .setMediaMetadata(metadata)
                                        .build());
                            }
                            int startIndex = args.getInt(ARG_START_INDEX, 0);
                            if (startIndex < 0 || startIndex >= items.size()) {
                                return result(SessionResult.RESULT_ERROR_BAD_VALUE);
                            }
                            player.startFolderQueue(items, startIndex,
                                    args.getBoolean(ARG_ENABLED, false),
                                    args.getBoolean(ARG_RANDOM, false));
                            return result(SessionResult.RESULT_SUCCESS);
                        }
                        return result(SessionResult.RESULT_ERROR_NOT_SUPPORTED);
                    }

                    private ListenableFuture<SessionResult> result(int code) {
                        return Futures.immediateFuture(new SessionResult(code));
                    }
                })
                .build();
        setShowNotificationForIdlePlayer(SHOW_NOTIFICATION_FOR_IDLE_PLAYER_NEVER);
    }

    @Override
    public MediaSession onGetSession(MediaSession.ControllerInfo controllerInfo) {
        return mediaSession;
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        if (!isPlaybackOngoing()) stopSelf();
    }

    @Override
    public void onDestroy() {
        if (mediaSession != null) {
            mediaSession.release();
            mediaSession = null;
        }
        if (player != null) {
            player.release();
            player = null;
        }
        super.onDestroy();
    }
}
