package app.mmfpocket.player;

import android.app.PendingIntent;
import android.content.Intent;

import androidx.media3.common.Player;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.session.CommandButton;
import androidx.media3.session.MediaSession;
import androidx.media3.session.MediaSessionService;

import java.util.Collections;

/** Owns MMF playback independently from the Activity lifecycle. */
@UnstableApi
public final class PlaybackService extends MediaSessionService {
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
