package app.mmfpocket.player;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.database.Cursor;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

import androidx.media3.common.C;
import androidx.media3.common.MediaItem;
import androidx.media3.common.Player;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.session.MediaController;
import androidx.media3.session.SessionToken;

import com.google.common.util.concurrent.ListenableFuture;

@UnstableApi
public final class MainActivity extends Activity {
    private static final int REQUEST_FOLDER = 1001;
    private static final String PREFS = "mmf_player";
    private static final String PREF_TREE_URI = "tree_uri";
    private static final String PREF_PHONE_SOUND = "phone_sound";
    private static final String PREF_CONTINUOUS_PLAYBACK = "continuous_playback";
    private static final String PREF_RANDOM_PLAYBACK = "random_playback";
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final ExecutorService batchWorker = Executors.newSingleThreadExecutor();
    private final Object batchPauseLock = new Object();
    private final AtomicInteger scanGeneration = new AtomicInteger();
    private final AtomicInteger batchGeneration = new AtomicInteger();
    private final Handler progressHandler = new Handler(Looper.getMainLooper());
    private final Runnable progressUpdater = new Runnable() {
        @Override
        public void run() {
            updatePlayerProgress();
            MediaController controller = mediaController;
            if (controller != null && (controller.isPlaying()
                    || controller.getPlaybackState() == Player.STATE_BUFFERING)) {
                progressHandler.postDelayed(this, 250);
            }
        }
    };
    private final Player.Listener controllerListener = new Player.Listener() {
        @Override
        public void onEvents(Player player, Player.Events events) {
            if (events.contains(Player.EVENT_POSITION_DISCONTINUITY)) {
                allowProgressRegression = true;
            }
            updateUiFromController();
        }
    };
    private final List<BrowserEntry> browserEntries = new ArrayList<>();
    private final List<MmfEntry> entries = new ArrayList<>();
    private final List<String> names = new ArrayList<>();
    private final ArrayDeque<FolderLocation> folderHistory = new ArrayDeque<>();

    private ArrayAdapter<String> adapter;
    private TextView folderLabel;
    private TextView parentFolderRow;
    private TextView batchStatusLabel;
    private TextView statusLabel;
    private IllustratedMafuMascotView mafuMascotView;
    private int mafuMotionPace = IllustratedMafuMascotView.PACE_NORMAL;
    private int mafuRoutineSeed;
    private TextView formatLabel;
    private TextView nowPlayingLabel;
    private TextView timeLabel;
    private ListView listView;
    private SeekBar playbackProgress;
    private Button chooseButton;
    private Button batchButton;
    private Button batchPauseButton;
    private ImageButton randomPlaybackButton;
    private Switch phoneSoundSwitch;
    private ImageButton continuousPlaybackButton;
    private ImageButton previousTrackButton;
    private ImageButton playPauseButton;
    private ImageButton nextTrackButton;
    private ImageButton stopButton;
    private String currentName;
    private MmfEntry selectedEntry;
    private PlaybackRequest pendingPlaybackRequest;
    private boolean pendingPhoneModeUpdate;
    private boolean pendingContinuousUpdate;
    private boolean pendingRandomUpdate;
    private boolean continuousPlaybackEnabled;
    private boolean randomPlaybackEnabled;
    private String progressMediaId;
    private int lastVisualProgress;
    private boolean allowProgressRegression;
    private ListenableFuture<MediaController> controllerFuture;
    private MediaController mediaController;
    private boolean activityStarted;
    private boolean batchConverting;
    private volatile boolean batchPaused;
    private volatile int batchCompletedCount;
    private volatile int batchTotalCount;
    private Uri selectedTreeUri;
    private FolderLocation currentFolder;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(buildUi());

        String saved = getSharedPreferences(PREFS, MODE_PRIVATE).getString(PREF_TREE_URI, null);
        if (saved != null) {
            loadFolder(Uri.parse(saved));
        }
    }

    private View buildUi() {
        int pad = dp(12);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad, pad, pad);
        root.setOnApplyWindowInsetsListener((view, windowInsets) -> {
            int left;
            int top;
            int right;
            int bottom;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                Insets bars = windowInsets.getInsets(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                left = bars.left;
                top = bars.top;
                right = bars.right;
                bottom = bars.bottom;
            } else {
                left = windowInsets.getSystemWindowInsetLeft();
                top = windowInsets.getSystemWindowInsetTop();
                right = windowInsets.getSystemWindowInsetRight();
                bottom = windowInsets.getSystemWindowInsetBottom();
            }
            view.setPadding(pad + left, pad + top, pad + right, pad + bottom);
            return windowInsets;
        });

        LinearLayout titleRow = new LinearLayout(this);
        titleRow.setOrientation(LinearLayout.HORIZONTAL);
        titleRow.setGravity(Gravity.CENTER_VERTICAL);

        TextView title = new TextView(this);
        title.setText(getString(R.string.app_name));
        title.setTextSize(22);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        titleRow.addView(title, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        batchStatusLabel = new TextView(this);
        batchStatusLabel.setTextSize(11);
        batchStatusLabel.setTextColor(0xFFFFFFFF);
        batchStatusLabel.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        batchStatusLabel.setSingleLine(true);
        batchStatusLabel.setGravity(Gravity.CENTER);
        batchStatusLabel.setPadding(dp(9), dp(5), dp(9), dp(5));
        batchStatusLabel.setBackgroundResource(R.drawable.batch_status_background);
        batchStatusLabel.setVisibility(View.GONE);
        titleRow.addView(batchStatusLabel, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        TextView versionLabel = new TextView(this);
        versionLabel.setText(getString(R.string.version_label, BuildConfig.VERSION_NAME));
        versionLabel.setTextSize(11);
        versionLabel.setTextColor(0xFF757575);
        versionLabel.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        versionLabel.setPadding(dp(8), 0, 0, 0);
        titleRow.addView(versionLabel, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.MATCH_PARENT));

        root.addView(titleRow, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        LinearLayout folderControls = new LinearLayout(this);
        folderControls.setOrientation(LinearLayout.HORIZONTAL);
        folderControls.setGravity(Gravity.CENTER_VERTICAL | Gravity.END);
        LinearLayout.LayoutParams folderControlsParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        folderControlsParams.topMargin = dp(2);

        folderLabel = new TextView(this);
        folderLabel.setText(R.string.no_folder_ja);
        folderLabel.setTextSize(12);
        folderLabel.setSingleLine(true);
        folderLabel.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        folderLabel.setPadding(0, 0, dp(6), 0);
        LinearLayout.LayoutParams folderLabelParams = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        folderLabelParams.gravity = Gravity.CENTER_VERTICAL;
        folderControls.addView(folderLabel, folderLabelParams);

        chooseButton = new Button(this);
        chooseButton.setText(R.string.choose_folder_compact);
        chooseButton.setTextSize(12);
        chooseButton.setMinWidth(0);
        chooseButton.setMinimumWidth(0);
        chooseButton.setMinHeight(0);
        chooseButton.setMinimumHeight(0);
        chooseButton.setOnClickListener(v -> chooseFolder());
        LinearLayout.LayoutParams chooseParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                dp(40));
        folderControls.addView(chooseButton, chooseParams);

        batchButton = new Button(this);
        batchButton.setText(R.string.convert_folder_compact);
        batchButton.setTextSize(12);
        batchButton.setMinWidth(0);
        batchButton.setMinimumWidth(0);
        batchButton.setMinHeight(0);
        batchButton.setMinimumHeight(0);
        batchButton.setEnabled(false);
        batchButton.setOnClickListener(v -> convertAllFiles());

        LinearLayout.LayoutParams batchParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                dp(40));
        batchParams.leftMargin = dp(4);
        folderControls.addView(batchButton, batchParams);

        batchPauseButton = new Button(this);
        batchPauseButton.setText(R.string.pause_batch_compact);
        batchPauseButton.setTextSize(12);
        batchPauseButton.setMinWidth(0);
        batchPauseButton.setMinimumWidth(0);
        batchPauseButton.setMinHeight(0);
        batchPauseButton.setMinimumHeight(0);
        batchPauseButton.setVisibility(View.GONE);
        batchPauseButton.setOnClickListener(v -> toggleBatchPause());
        LinearLayout.LayoutParams batchPauseParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                dp(40));
        batchPauseParams.leftMargin = dp(4);
        folderControls.addView(batchPauseButton, batchPauseParams);
        root.addView(folderControls, folderControlsParams);

        parentFolderRow = new TextView(this);
        parentFolderRow.setText(R.string.parent_folder);
        parentFolderRow.setTextSize(16);
        parentFolderRow.setGravity(Gravity.CENTER_VERTICAL);
        parentFolderRow.setMinHeight(dp(44));
        parentFolderRow.setPadding(dp(16), 0, dp(16), 0);
        parentFolderRow.setVisibility(View.GONE);
        parentFolderRow.setOnClickListener(v -> navigateUp());
        TypedValue selectableBackground = new TypedValue();
        if (getTheme().resolveAttribute(
                android.R.attr.selectableItemBackground, selectableBackground, true)) {
            parentFolderRow.setBackgroundResource(selectableBackground.resourceId);
        }
        root.addView(parentFolderRow, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        listView = new ListView(this);
        listView.setChoiceMode(ListView.CHOICE_MODE_SINGLE);
        adapter = new ArrayAdapter<String>(
                this, android.R.layout.simple_list_item_activated_1, names) {
            @Override
            public View getView(int position, View convertView, ViewGroup parent) {
                TextView row = (TextView) super.getView(position, convertView, parent);
                row.setBackgroundResource(R.drawable.list_item_background);
                row.setTextColor(0xFF212121);
                if (position < browserEntries.size()
                        && browserEntries.get(position).kind == BrowserEntry.DIRECTORY) {
                    row.setCompoundDrawablesRelativeWithIntrinsicBounds(
                            R.drawable.ic_folder_outline, 0, 0, 0);
                    row.setCompoundDrawablePadding(dp(12));
                } else {
                    row.setCompoundDrawablesRelativeWithIntrinsicBounds(0, 0, 0, 0);
                    row.setCompoundDrawablePadding(0);
                }
                return row;
            }
        };
        listView.setAdapter(adapter);
        listView.setOnItemClickListener((parent, view, position, id) -> {
            BrowserEntry entry = browserEntries.get(position);
            if (entry.kind == BrowserEntry.DIRECTORY) {
                navigateInto(entry);
            } else {
                play(entry.file);
            }
        });
        root.addView(listView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));

        LinearLayout playerPanel = new LinearLayout(this);
        playerPanel.setOrientation(LinearLayout.VERTICAL);
        playerPanel.setPadding(dp(12), dp(6), dp(12), dp(6));
        playerPanel.setBackgroundResource(R.drawable.player_panel);
        LinearLayout.LayoutParams playerParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        playerParams.topMargin = dp(8);

        LinearLayout playerUpper = new LinearLayout(this);
        playerUpper.setOrientation(LinearLayout.HORIZONTAL);
        playerUpper.setGravity(Gravity.CENTER_VERTICAL);

        LinearLayout playerUpperContent = new LinearLayout(this);
        playerUpperContent.setOrientation(LinearLayout.VERTICAL);
        playerUpperContent.setGravity(Gravity.CENTER_VERTICAL);

        LinearLayout playerTop = new LinearLayout(this);
        playerTop.setOrientation(LinearLayout.HORIZONTAL);
        playerTop.setGravity(Gravity.CENTER_VERTICAL);

        mafuMascotView = new IllustratedMafuMascotView(this);
        mafuMascotView.setAlpha(0.72f);
        LinearLayout.LayoutParams mascotParams = new LinearLayout.LayoutParams(dp(84), dp(92));
        mascotParams.rightMargin = dp(6);
        playerUpper.addView(mafuMascotView, mascotParams);

        LinearLayout trackInfo = new LinearLayout(this);
        trackInfo.setOrientation(LinearLayout.VERTICAL);
        trackInfo.setGravity(Gravity.CENTER_VERTICAL);

        nowPlayingLabel = new TextView(this);
        nowPlayingLabel.setText(R.string.no_track_selected);
        nowPlayingLabel.setTextSize(15);
        nowPlayingLabel.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        nowPlayingLabel.setSingleLine(true);
        nowPlayingLabel.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        trackInfo.addView(nowPlayingLabel);

        statusLabel = new TextView(this);
        statusLabel.setText(R.string.select_mmf_prompt);
        statusLabel.setTextSize(12);
        statusLabel.setTextColor(0xFF1E88E5);
        statusLabel.setSingleLine(true);
        statusLabel.setEllipsize(TextUtils.TruncateAt.END);
        trackInfo.addView(statusLabel);

        playerTop.addView(trackInfo, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        LinearLayout playerMetadata = new LinearLayout(this);
        playerMetadata.setOrientation(LinearLayout.VERTICAL);
        playerMetadata.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);

        formatLabel = new TextView(this);
        formatLabel.setTextSize(12);
        formatLabel.setTextColor(0xFF9E9E9E);
        formatLabel.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        formatLabel.setGravity(Gravity.END);
        formatLabel.setSingleLine(true);
        playerMetadata.addView(formatLabel);

        phoneSoundSwitch = new Switch(this);
        phoneSoundSwitch.setText(R.string.phone_sound_mode_short);
        phoneSoundSwitch.setTextSize(9);
        phoneSoundSwitch.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        phoneSoundSwitch.setChecked(getSharedPreferences(PREFS, MODE_PRIVATE)
                .getBoolean(PREF_PHONE_SOUND, false));
        phoneSoundSwitch.setOnCheckedChangeListener((button, checked) -> {
            getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                    .putBoolean(PREF_PHONE_SOUND, checked).apply();
            updatePhoneSoundMode(checked);
            statusLabel.setText(checked ? R.string.phone_mode_enabled : R.string.phone_mode_disabled);
        });
        playerMetadata.addView(phoneSoundSwitch);
        playerTop.addView(playerMetadata);
        playerUpperContent.addView(playerTop, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(52)));

        playbackProgress = new SeekBar(this);
        playbackProgress.setMax(1000);
        playbackProgress.setEnabled(false);
        playbackProgress.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                MediaController controller = mediaController;
                if (!fromUser || controller == null) return;
                long duration = controller.getDuration();
                if (duration == C.TIME_UNSET || duration <= 0) return;
                long requested = duration * progress / seekBar.getMax();
                long preview = Math.min(requested, controller.getBufferedPosition());
                timeLabel.setText(getString(R.string.playback_time,
                        formatTime(preview), formatTime(duration)));
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
                progressHandler.removeCallbacks(progressUpdater);
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                MediaController controller = mediaController;
                if (controller == null) return;
                long duration = controller.getDuration();
                if (duration == C.TIME_UNSET || duration <= 0) return;
                long requested = duration * seekBar.getProgress() / seekBar.getMax();
                allowProgressRegression = true;
                controller.seekTo(Math.min(requested, controller.getBufferedPosition()));
                if (controller.isPlaying()) startProgressUpdates();
                else updatePlayerProgress();
            }
        });
        playbackProgress.setPadding(dp(10), 0, dp(10), 0);
        LinearLayout progressRow = new LinearLayout(this);
        progressRow.setOrientation(LinearLayout.HORIZONTAL);
        progressRow.setGravity(Gravity.CENTER_VERTICAL);
        progressRow.addView(playbackProgress, new LinearLayout.LayoutParams(
                0, dp(32), 1));
        playerUpperContent.addView(progressRow, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(32)));
        playerUpper.addView(playerUpperContent, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 1));
        playerPanel.addView(playerUpper, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(92)));

        timeLabel = new TextView(this);
        timeLabel.setText(R.string.zero_playback_time);
        timeLabel.setTextSize(11);
        timeLabel.setTextColor(0xFF68707B);
        timeLabel.setGravity(Gravity.START | Gravity.TOP);

        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.HORIZONTAL);
        controls.setGravity(Gravity.CENTER_VERTICAL);

        ColorStateList modeIconColors = new ColorStateList(
                new int[][] {
                        new int[] {-android.R.attr.state_enabled},
                        new int[] {}
                },
                new int[] {0xFFBDBDBD, 0xFF283593});

        continuousPlaybackEnabled = getSharedPreferences(PREFS, MODE_PRIVATE)
                .getBoolean(PREF_CONTINUOUS_PLAYBACK, false);

        LinearLayout transportControls = new LinearLayout(this);
        transportControls.setOrientation(LinearLayout.HORIZONTAL);
        transportControls.setGravity(Gravity.CENTER);

        LinearLayout modeControls = new LinearLayout(this);
        modeControls.setOrientation(LinearLayout.HORIZONTAL);
        modeControls.setGravity(Gravity.CENTER_VERTICAL | Gravity.END);

        continuousPlaybackButton = new ImageButton(this);
        continuousPlaybackButton.setImageResource(R.drawable.ic_repeat_all);
        continuousPlaybackButton.setImageTintList(modeIconColors);
        continuousPlaybackButton.setBackgroundResource(R.drawable.mode_button_background);
        continuousPlaybackButton.setPadding(dp(8), dp(8), dp(8), dp(8));
        continuousPlaybackButton.setOnClickListener(v -> setContinuousPlaybackEnabled(
                !continuousPlaybackEnabled, true));
        updateContinuousPlaybackButton();
        modeControls.addView(continuousPlaybackButton,
                new LinearLayout.LayoutParams(dp(38), dp(38)));

        previousTrackButton = new ImageButton(this);
        previousTrackButton.setImageResource(R.drawable.ic_skip_previous);
        previousTrackButton.setContentDescription(
                getString(R.string.previous_track_description));
        previousTrackButton.setBackgroundResource(R.drawable.control_button_background);
        previousTrackButton.setPadding(dp(9), dp(9), dp(9), dp(9));
        previousTrackButton.setEnabled(false);
        previousTrackButton.setOnClickListener(v -> skipToPreviousTrack());
        transportControls.addView(previousTrackButton,
                new LinearLayout.LayoutParams(dp(38), dp(38)));

        playPauseButton = new ImageButton(this);
        playPauseButton.setImageResource(R.drawable.ic_play);
        playPauseButton.setContentDescription(getString(R.string.play_description));
        playPauseButton.setBackgroundResource(R.drawable.primary_control_button_background);
        playPauseButton.setImageTintList(new ColorStateList(
                new int[][] {
                        new int[] {-android.R.attr.state_enabled},
                        new int[] {}
                },
                new int[] {0xFFEEEEEE, 0xFFFFFFFF}));
        playPauseButton.setPadding(dp(11), dp(11), dp(11), dp(11));
        playPauseButton.setEnabled(false);
        playPauseButton.setOnClickListener(v -> togglePlayback());
        LinearLayout.LayoutParams playParams = new LinearLayout.LayoutParams(dp(52), dp(52));
        playParams.leftMargin = dp(3);
        transportControls.addView(playPauseButton, playParams);

        nextTrackButton = new ImageButton(this);
        nextTrackButton.setImageResource(R.drawable.ic_skip_next);
        nextTrackButton.setContentDescription(getString(R.string.next_track_description));
        nextTrackButton.setBackgroundResource(R.drawable.control_button_background);
        nextTrackButton.setPadding(dp(9), dp(9), dp(9), dp(9));
        nextTrackButton.setEnabled(false);
        nextTrackButton.setOnClickListener(v -> skipToNextTrack());
        LinearLayout.LayoutParams nextParams = new LinearLayout.LayoutParams(dp(38), dp(38));
        nextParams.leftMargin = dp(3);

        stopButton = new ImageButton(this);
        stopButton.setImageResource(R.drawable.ic_stop);
        stopButton.setContentDescription(getString(R.string.stop_description));
        stopButton.setBackgroundResource(R.drawable.control_button_background);
        stopButton.setPadding(dp(9), dp(9), dp(9), dp(9));
        stopButton.setEnabled(false);
        stopButton.setOnClickListener(v -> stopPlayback(true));
        LinearLayout.LayoutParams stopParams = new LinearLayout.LayoutParams(dp(38), dp(38));
        stopParams.leftMargin = dp(3);
        transportControls.addView(stopButton, stopParams);
        transportControls.addView(nextTrackButton, nextParams);

        randomPlaybackButton = new ImageButton(this);
        randomPlaybackButton.setImageResource(R.drawable.ic_shuffle);
        randomPlaybackButton.setImageTintList(modeIconColors);
        randomPlaybackButton.setBackgroundResource(R.drawable.mode_button_background);
        randomPlaybackButton.setPadding(dp(8), dp(8), dp(8), dp(8));
        randomPlaybackEnabled = getSharedPreferences(PREFS, MODE_PRIVATE)
                .getBoolean(PREF_RANDOM_PLAYBACK, false);
        randomPlaybackButton.setOnClickListener(v -> setRandomPlaybackEnabled(
                !randomPlaybackEnabled, true));
        updateRandomPlaybackButton();
        LinearLayout.LayoutParams randomControlParams =
                new LinearLayout.LayoutParams(dp(38), dp(38));
        randomControlParams.leftMargin = dp(4);
        modeControls.addView(randomPlaybackButton, randomControlParams);

        controls.addView(timeLabel, new LinearLayout.LayoutParams(dp(72), dp(52)));
        LinearLayout.LayoutParams transportParams = new LinearLayout.LayoutParams(
                0, dp(52), 1);
        controls.addView(transportControls, transportParams);
        View modeDivider = new View(this);
        modeDivider.setBackgroundColor(0xFFE0E0E0);
        LinearLayout.LayoutParams dividerParams = new LinearLayout.LayoutParams(dp(1), dp(34));
        dividerParams.leftMargin = dp(4);
        dividerParams.rightMargin = dp(6);
        controls.addView(modeDivider, dividerParams);
        LinearLayout.LayoutParams modeParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, dp(52));
        controls.addView(modeControls, modeParams);
        playerPanel.addView(controls, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(52)));

        root.addView(playerPanel, playerParams);
        return root;
    }

    private void chooseFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_FOLDER);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_FOLDER || resultCode != RESULT_OK || data == null) return;

        Uri uri = data.getData();
        if (uri == null) return;
        if ((data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) != 0) {
            try {
                getContentResolver().takePersistableUriPermission(
                        uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException ignored) {
                // The current process still has read access if a provider cannot persist it.
            }
        }
        getSharedPreferences(PREFS, MODE_PRIVATE)
                .edit().putString(PREF_TREE_URI, uri.toString()).apply();
        loadFolder(uri);
    }

    private void loadFolder(Uri treeUri) {
        selectedEntry = null;
        selectedTreeUri = treeUri;
        folderHistory.clear();
        try {
            String rootId = DocumentsContract.getTreeDocumentId(treeUri);
            openDirectory(new FolderLocation(rootId, folderDisplayName(treeUri)));
        } catch (Exception error) {
            Toast.makeText(this, getString(R.string.folder_read_failed, error.getMessage()),
                    Toast.LENGTH_LONG).show();
        }
    }

    private void openDirectory(FolderLocation folder) {
        if (selectedTreeUri == null) return;
        currentFolder = folder;
        Uri treeUri = selectedTreeUri;
        int generation = scanGeneration.incrementAndGet();
        folderLabel.setText(getString(R.string.selected_folder, folder.displayPath));
        parentFolderRow.setVisibility(folderHistory.isEmpty() ? View.GONE : View.VISIBLE);
        statusLabel.setText(R.string.scanning_folder);
        batchButton.setEnabled(false);
        listView.setEnabled(false);
        worker.execute(() -> {
            List<BrowserEntry> found = queryDirectory(treeUri, folder.documentId);
            found.sort(Comparator
                    .comparingInt((BrowserEntry entry) -> entry.kind)
                    .thenComparing(entry -> entry.name.toLowerCase(Locale.ROOT)));
            runOnUiThread(() -> {
                if (generation != scanGeneration.get() || isFinishing() || isDestroyed()) return;
                browserEntries.clear();
                names.clear();
                entries.clear();
                int folderCount = 0;
                for (BrowserEntry entry : found) {
                    browserEntries.add(entry);
                    if (entry.kind == BrowserEntry.DIRECTORY) {
                        folderCount++;
                        names.add(entry.name);
                    } else {
                        entries.add(entry.file);
                        names.add(entry.name);
                    }
                }
                adapter.notifyDataSetChanged();
                listView.clearChoices();
                listView.setEnabled(true);
                batchButton.setEnabled(!entries.isEmpty() && !batchConverting);
                statusLabel.setText(found.isEmpty()
                        ? getString(R.string.empty_folder_ja)
                        : getString(R.string.folder_item_count, entries.size(), folderCount));
            });
        });
    }

    private List<BrowserEntry> queryDirectory(Uri treeUri, String documentId) {
        List<BrowserEntry> result = new ArrayList<>();
        try {
            String[] projection = {
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE
            };
            Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                    treeUri, documentId);
            try (Cursor cursor = getContentResolver().query(
                    childrenUri, projection, null, null, null)) {
                if (cursor == null) return result;
                while (cursor.moveToNext()) {
                    String childId = cursor.getString(0);
                    String name = cursor.getString(1);
                    String mime = cursor.getString(2);
                    if (name == null) continue;
                    if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                        result.add(BrowserEntry.directory(name, childId));
                    } else if (name.toLowerCase(Locale.ROOT).endsWith(".mmf")) {
                        Uri fileUri = DocumentsContract.buildDocumentUriUsingTree(
                                treeUri, childId);
                        result.add(BrowserEntry.file(new MmfEntry(name, fileUri)));
                    }
                }
            }
        } catch (Exception error) {
            runOnUiThread(() -> Toast.makeText(this,
                    getString(R.string.folder_read_failed, error.getMessage()),
                    Toast.LENGTH_LONG).show());
        }
        return result;
    }

    private void navigateInto(BrowserEntry directory) {
        if (currentFolder == null) return;
        folderHistory.addLast(currentFolder);
        openDirectory(new FolderLocation(directory.documentId,
                currentFolder.displayPath + "/" + directory.name));
    }

    private void navigateUp() {
        if (folderHistory.isEmpty()) return;
        openDirectory(folderHistory.removeLast());
    }

    private void play(MmfEntry entry) {
        List<MmfEntry> queue = new ArrayList<>(entries);
        int startIndex = indexOfEntry(queue, entry.uri.toString());
        if (startIndex == C.INDEX_UNSET) {
            queue.clear();
            queue.add(entry);
            startIndex = 0;
        }
        beginPlayback(new PlaybackRequest(queue, startIndex));
    }

    private void beginPlayback(PlaybackRequest request) {
        MmfEntry entry = request.queue.get(request.startIndex);
        selectedEntry = entry;
        currentName = entry.name;
        nowPlayingLabel.setText(entry.name);
        formatLabel.setText("");
        playPauseButton.setEnabled(true);
        stopButton.setEnabled(true);
        statusLabel.setText(R.string.player_status_converting);
        MediaController controller = mediaController;
        if (controller == null) {
            pendingPlaybackRequest = request;
            connectController();
            return;
        }
        startPlayback(controller, request);
    }

    private void startPlayback(MediaController controller, PlaybackRequest request) {
        pendingPlaybackRequest = null;
        pendingPhoneModeUpdate = false;
        pendingContinuousUpdate = false;
        pendingRandomUpdate = false;
        ArrayList<String> uris = new ArrayList<>(request.queue.size());
        ArrayList<String> names = new ArrayList<>(request.queue.size());
        for (MmfEntry entry : request.queue) {
            uris.add(entry.uri.toString());
            names.add(entry.name);
        }
        Bundle args = new Bundle();
        args.putStringArrayList(PlaybackService.ARG_URIS, uris);
        args.putStringArrayList(PlaybackService.ARG_NAMES, names);
        args.putInt(PlaybackService.ARG_START_INDEX, request.startIndex);
        args.putBoolean(PlaybackService.ARG_ENABLED, continuousPlaybackEnabled);
        args.putBoolean(PlaybackService.ARG_RANDOM, randomPlaybackEnabled);
        args.putBoolean(PlaybackService.ARG_PHONE_SOUND, phoneSoundSwitch.isChecked());
        controller.sendCustomCommand(PlaybackService.COMMAND_START_FOLDER_QUEUE, args);
        showPauseIcon();
        startProgressUpdates();
    }

    private void convertAllFiles() {
        if (entries.isEmpty() || batchConverting) return;
        int generation = batchGeneration.incrementAndGet();
        List<MmfEntry> snapshot = new ArrayList<>(entries);
        boolean phoneSpeakerMode = phoneSoundSwitch.isChecked();
        batchConverting = true;
        batchPaused = false;
        batchCompletedCount = 0;
        batchTotalCount = snapshot.size();
        batchButton.setEnabled(false);
        batchPauseButton.setText(R.string.pause_batch_compact);
        batchPauseButton.setVisibility(View.VISIBLE);
        batchStatusLabel.setText(getString(R.string.batch_progress_compact, 0, snapshot.size()));
        batchStatusLabel.setVisibility(View.VISIBLE);

        batchWorker.execute(() -> {
            android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_BACKGROUND);
            int converted = 0;
            int reused = 0;
            int failed = 0;
            for (int i = 0; i < snapshot.size(); i++) {
                if (generation != batchGeneration.get() || Thread.currentThread().isInterrupted()) return;
                if (!waitWhileBatchPaused(generation, i, snapshot.size())) return;
                MmfEntry entry = snapshot.get(i);
                int position = i + 1;
                runOnUiThread(() -> {
                    if (generation == batchGeneration.get() && !batchPaused) {
                        batchStatusLabel.setText(getString(R.string.batch_progress_compact,
                                position, snapshot.size()));
                        batchStatusLabel.setContentDescription(getString(R.string.batch_progress,
                                position, snapshot.size(), entry.name));
                    }
                });
                try {
                    MmfAudioRepository.RenderResult result =
                            MmfAudioRepository.renderOrGetCached(
                                    this, entry.uri, phoneSpeakerMode);
                    if (!result.error.isEmpty()) {
                        failed++;
                    } else if (result.cacheHit) {
                        reused++;
                    } else {
                        converted++;
                    }
                } catch (Exception ignored) {
                    failed++;
                }
                batchCompletedCount = position;
            }

            int finalConverted = converted;
            int finalReused = reused;
            int finalFailed = failed;
            runOnUiThread(() -> {
                if (generation != batchGeneration.get() || isFinishing() || isDestroyed()) return;
                batchConverting = false;
                batchPaused = false;
                batchButton.setEnabled(!entries.isEmpty());
                batchPauseButton.setVisibility(View.GONE);
                batchStatusLabel.setVisibility(View.GONE);
                Toast.makeText(this, getString(R.string.batch_complete,
                        finalConverted, finalReused, finalFailed), Toast.LENGTH_LONG).show();
            });
        });
    }

    private boolean waitWhileBatchPaused(int generation, int completed, int total) {
        synchronized (batchPauseLock) {
            while (batchPaused && generation == batchGeneration.get()) {
                runOnUiThread(() -> {
                    if (generation == batchGeneration.get() && batchPaused) {
                        batchStatusLabel.setText(getString(R.string.batch_paused_compact,
                                completed, total));
                    }
                });
                try {
                    batchPauseLock.wait();
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    return false;
                }
            }
        }
        return generation == batchGeneration.get() && !Thread.currentThread().isInterrupted();
    }

    private void toggleBatchPause() {
        if (!batchConverting) return;
        synchronized (batchPauseLock) {
            batchPaused = !batchPaused;
            if (!batchPaused) batchPauseLock.notifyAll();
        }
        if (batchPaused) {
            batchPauseButton.setText(R.string.resume_batch_compact);
            batchStatusLabel.setText(getString(R.string.batch_pause_pending_compact,
                    batchCompletedCount, batchTotalCount));
        } else {
            batchPauseButton.setText(R.string.pause_batch_compact);
            batchStatusLabel.setText(getString(R.string.batch_progress_compact,
                    batchCompletedCount, batchTotalCount));
        }
    }

    private void cancelBatchConversion() {
        batchGeneration.incrementAndGet();
        if (!batchConverting) return;
        batchConverting = false;
        synchronized (batchPauseLock) {
            batchPaused = false;
            batchPauseLock.notifyAll();
        }
        batchButton.setEnabled(!entries.isEmpty());
        batchPauseButton.setVisibility(View.GONE);
        batchStatusLabel.setVisibility(View.GONE);
    }

    private void connectController() {
        if (!activityStarted || mediaController != null || controllerFuture != null) return;
        SessionToken token = new SessionToken(
                this, new ComponentName(this, PlaybackService.class));
        ListenableFuture<MediaController> future =
                new MediaController.Builder(this, token).buildAsync();
        controllerFuture = future;
        future.addListener(() -> {
            try {
                MediaController controller = future.get();
                if (!activityStarted || controllerFuture != future) {
                    controller.release();
                    return;
                }
                controllerFuture = null;
                mediaController = controller;
                controller.addListener(controllerListener);
                updateUiFromController();
                PlaybackRequest pending = pendingPlaybackRequest;
                if (pending != null) {
                    startPlayback(controller, pending);
                } else {
                    if (pendingContinuousUpdate) {
                        updateContinuousPlayback(continuousPlaybackEnabled);
                    }
                    if (pendingRandomUpdate) {
                        updateRandomPlayback(randomPlaybackEnabled);
                    }
                    if (pendingPhoneModeUpdate) {
                        updatePhoneSoundMode(phoneSoundSwitch.isChecked());
                    }
                }
            } catch (Exception error) {
                if (controllerFuture == future) controllerFuture = null;
                updateMafuMascot(false);
                statusLabel.setText(getString(R.string.player_connection_failed));
            }
        }, getMainExecutor());
    }

    private void disconnectController() {
        progressHandler.removeCallbacks(progressUpdater);
        MediaController controller = mediaController;
        mediaController = null;
        if (controller != null) {
            controller.removeListener(controllerListener);
            controller.release();
        }
        ListenableFuture<MediaController> future = controllerFuture;
        controllerFuture = null;
        if (future != null) MediaController.releaseFuture(future);
    }

    private void updateUiFromController() {
        MediaController controller = mediaController;
        if (controller == null) return;
        MediaItem item = controller.getCurrentMediaItem();
        String name = currentName;
        if (item != null && item.mediaMetadata.title != null) {
            if (!item.mediaId.equals(progressMediaId)) {
                progressMediaId = item.mediaId;
                lastVisualProgress = 0;
                allowProgressRegression = true;
            }
            name = item.mediaMetadata.title.toString();
            currentName = name;
            mafuRoutineSeed = item.mediaId.hashCode();
            nowPlayingLabel.setText(name);
            Bundle extras = item.mediaMetadata.extras;
            formatLabel.setText(extras == null ? ""
                    : extras.getString(MmfPlayer.EXTRA_FORMAT_LABEL, ""));
            mafuMotionPace = extras == null ? IllustratedMafuMascotView.PACE_NORMAL
                    : extras.getInt(MmfPlayer.EXTRA_MOTION_PACE,
                            IllustratedMafuMascotView.PACE_NORMAL);
            int visibleIndex = indexOfBrowserEntry(item.mediaId);
            if (visibleIndex == C.INDEX_UNSET) {
                listView.clearChoices();
            } else {
                selectedEntry = browserEntries.get(visibleIndex).file;
                listView.setItemChecked(visibleIndex, true);
            }
        }

        int state = controller.getPlaybackState();
        boolean hasItem = item != null;
        playPauseButton.setEnabled(hasItem || selectedEntry != null);
        previousTrackButton.setEnabled(hasItem && controller.getMediaItemCount() > 1);
        nextTrackButton.setEnabled(hasItem && controller.getMediaItemCount() > 1);
        stopButton.setEnabled(hasItem && state != Player.STATE_IDLE);
        if (controller.isPlaying()
                || controller.getPlayWhenReady() && state == Player.STATE_BUFFERING) {
            showPauseIcon();
        } else {
            showPlayIcon();
        }

        if (controller.getPlayerError() != null) {
            statusLabel.setText(getString(R.string.playback_failed,
                    controller.getPlayerError().getMessage()));
        } else if (state == Player.STATE_BUFFERING && name != null) {
            statusLabel.setText(R.string.player_status_converting);
        } else if (state == Player.STATE_READY && name != null) {
            if (controller.isPlaying()) {
                statusLabel.setText(R.string.player_status_playing);
            } else if (controller.getPlaybackSuppressionReason()
                    == Player.PLAYBACK_SUPPRESSION_REASON_TRANSIENT_AUDIO_FOCUS_LOSS) {
                statusLabel.setText(R.string.player_status_audio_focus_paused);
            } else {
                statusLabel.setText(R.string.player_status_paused);
            }
        } else if (state == Player.STATE_ENDED && name != null) {
            statusLabel.setText(R.string.player_status_finished);
        } else if (state == Player.STATE_IDLE && hasItem) {
            statusLabel.setText(R.string.playback_stopped);
        }

        updateMafuMascot(controller.isPlaying());
        updatePlayerProgress();
        if (controller.isPlaying() || state == Player.STATE_BUFFERING) startProgressUpdates();
        else progressHandler.removeCallbacks(progressUpdater);
    }

    private void togglePlayback() {
        MediaController controller = mediaController;
        if (controller == null || controller.getCurrentMediaItem() == null) {
            if (selectedEntry != null) play(selectedEntry);
            return;
        }
        if (controller.isPlaying() || controller.getPlayWhenReady()) {
            controller.pause();
            progressHandler.removeCallbacks(progressUpdater);
        } else {
            controller.play();
            startProgressUpdates();
        }
    }

    private void skipToPreviousTrack() {
        MediaController controller = mediaController;
        if (controller == null || controller.getCurrentMediaItem() == null) return;
        controller.sendCustomCommand(PlaybackService.COMMAND_SKIP_PREVIOUS, Bundle.EMPTY);
        startProgressUpdates();
    }

    private void skipToNextTrack() {
        MediaController controller = mediaController;
        if (controller == null || controller.getCurrentMediaItem() == null) return;
        controller.sendCustomCommand(PlaybackService.COMMAND_SKIP_NEXT, Bundle.EMPTY);
        startProgressUpdates();
    }

    private void stopPlayback(boolean updateStatus) {
        MediaController controller = mediaController;
        if (controller != null) controller.stop();
        progressHandler.removeCallbacks(progressUpdater);
        showPlayIcon();
        stopButton.setEnabled(false);
        playbackProgress.setProgress(0);
        playbackProgress.setSecondaryProgress(0);
        playbackProgress.setEnabled(false);
        lastVisualProgress = 0;
        allowProgressRegression = true;
        timeLabel.setText(R.string.zero_playback_time);
        updateMafuMascot(false);
        if (updateStatus) statusLabel.setText(R.string.playback_stopped);
    }

    private void updateMafuMascot(boolean playing) {
        if (mafuMascotView == null) return;
        mafuMascotView.setAlpha(playing ? 1f : 0.72f);
        mafuMascotView.setPlaybackState(playing, mafuMotionPace, mafuRoutineSeed);
    }

    private void updatePhoneSoundMode(boolean enabled) {
        MediaController controller = mediaController;
        if (controller == null) {
            pendingPhoneModeUpdate = true;
            connectController();
            stopPlayback(false);
            return;
        }
        pendingPhoneModeUpdate = false;
        Bundle args = new Bundle();
        args.putBoolean(PlaybackService.ARG_ENABLED, enabled);
        controller.sendCustomCommand(PlaybackService.COMMAND_SET_PHONE_SOUND, args);
    }

    private void updateContinuousPlayback(boolean enabled) {
        MediaController controller = mediaController;
        if (controller == null) {
            pendingContinuousUpdate = true;
            connectController();
            return;
        }
        pendingContinuousUpdate = false;
        Bundle args = new Bundle();
        args.putBoolean(PlaybackService.ARG_ENABLED, enabled);
        controller.sendCustomCommand(PlaybackService.COMMAND_SET_CONTINUOUS, args);
    }

    private void setContinuousPlaybackEnabled(boolean enabled, boolean notifyService) {
        boolean turningOn = enabled && !continuousPlaybackEnabled;
        continuousPlaybackEnabled = enabled;
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                .putBoolean(PREF_CONTINUOUS_PLAYBACK, enabled).apply();
        if (turningOn && randomPlaybackEnabled) {
            setRandomPlaybackEnabled(false, notifyService);
        }
        if (continuousPlaybackButton != null) {
            updateContinuousPlaybackButton();
            updateRandomPlaybackButton();
        }
        if (notifyService) updateContinuousPlayback(enabled);
    }

    private void updateContinuousPlaybackButton() {
        if (continuousPlaybackButton == null) return;
        continuousPlaybackButton.setSelected(continuousPlaybackEnabled);
        continuousPlaybackButton.setContentDescription(getString(continuousPlaybackEnabled
                ? R.string.continuous_playback_on_description
                : R.string.continuous_playback_off_description));
    }

    private void updateRandomPlayback(boolean enabled) {
        MediaController controller = mediaController;
        if (controller == null) {
            pendingRandomUpdate = true;
            connectController();
            return;
        }
        pendingRandomUpdate = false;
        Bundle args = new Bundle();
        args.putBoolean(PlaybackService.ARG_ENABLED, enabled);
        controller.sendCustomCommand(PlaybackService.COMMAND_SET_RANDOM, args);
    }

    private void setRandomPlaybackEnabled(boolean enabled, boolean notifyService) {
        randomPlaybackEnabled = enabled;
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                .putBoolean(PREF_RANDOM_PLAYBACK, enabled).apply();
        updateRandomPlaybackButton();
        if (notifyService) updateRandomPlayback(enabled);
    }

    private void updateRandomPlaybackButton() {
        if (randomPlaybackButton == null) return;
        randomPlaybackButton.setEnabled(continuousPlaybackEnabled);
        randomPlaybackButton.setSelected(randomPlaybackEnabled);
        randomPlaybackButton.setContentDescription(getString(!continuousPlaybackEnabled
                ? R.string.random_playback_unavailable_description
                : randomPlaybackEnabled
                        ? R.string.random_playback_on_description
                        : R.string.random_playback_off_description));
    }

    private int indexOfEntry(List<MmfEntry> queue, String mediaId) {
        for (int index = 0; index < queue.size(); index++) {
            if (queue.get(index).uri.toString().equals(mediaId)) return index;
        }
        return C.INDEX_UNSET;
    }

    private int indexOfBrowserEntry(String mediaId) {
        for (int index = 0; index < browserEntries.size(); index++) {
            BrowserEntry entry = browserEntries.get(index);
            if (entry.kind == BrowserEntry.FILE
                    && entry.file.uri.toString().equals(mediaId)) return index;
        }
        return C.INDEX_UNSET;
    }

    private void startProgressUpdates() {
        progressHandler.removeCallbacks(progressUpdater);
        progressUpdater.run();
    }

    private void showPlayIcon() {
        playPauseButton.setImageResource(R.drawable.ic_play);
        playPauseButton.setContentDescription(getString(R.string.play_description));
    }

    private void showPauseIcon() {
        playPauseButton.setImageResource(R.drawable.ic_pause);
        playPauseButton.setContentDescription(getString(R.string.pause_description));
    }

    private String folderDisplayName(Uri treeUri) {
        try {
            String documentId = DocumentsContract.getTreeDocumentId(treeUri);
            int separator = documentId.indexOf(':');
            String path = separator >= 0 ? documentId.substring(separator + 1) : documentId;
            return path.isEmpty() ? getString(R.string.internal_storage) : path;
        } catch (Exception ignored) {
            return getString(R.string.selected_folder_fallback);
        }
    }

    private void updatePlayerProgress() {
        MediaController controller = mediaController;
        if (controller == null) return;
        long duration = controller.getDuration();
        if (duration == C.TIME_UNSET || duration <= 0) {
            playbackProgress.setEnabled(false);
            return;
        }
        long position = Math.max(0, controller.getCurrentPosition());
        long buffered = Math.max(position, controller.getBufferedPosition());
        int max = playbackProgress.getMax();
        playbackProgress.setEnabled(controller.getPlaybackState() != Player.STATE_IDLE);
        int visualProgress = (int) (Math.min(position, duration) * max / duration);
        if (!allowProgressRegression) {
            visualProgress = Math.max(lastVisualProgress, visualProgress);
        }
        lastVisualProgress = visualProgress;
        allowProgressRegression = false;
        playbackProgress.setProgress(visualProgress);
        playbackProgress.setSecondaryProgress(
                (int) (Math.min(buffered, duration) * max / duration));
        timeLabel.setText(getString(R.string.playback_time,
                formatTime(position), formatTime(duration)));
    }

    private String formatTime(long milliseconds) {
        long totalSeconds = Math.max(0, milliseconds) / 1000;
        return String.format(Locale.ROOT, "%d:%02d", totalSeconds / 60, totalSeconds % 60);
    }

    @Override
    protected void onStart() {
        super.onStart();
        activityStarted = true;
        connectController();
    }

    @Override
    protected void onStop() {
        cancelBatchConversion();
        activityStarted = false;
        updateMafuMascot(false);
        disconnectController();
        super.onStop();
    }

    @Override
    public void onBackPressed() {
        if (!folderHistory.isEmpty()) {
            navigateUp();
            return;
        }
        super.onBackPressed();
    }

    @Override
    protected void onDestroy() {
        scanGeneration.incrementAndGet();
        batchGeneration.incrementAndGet();
        progressHandler.removeCallbacks(progressUpdater);
        worker.shutdownNow();
        batchWorker.shutdownNow();
        super.onDestroy();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private static final class MmfEntry {
        final String name;
        final Uri uri;

        MmfEntry(String name, Uri uri) {
            this.name = name;
            this.uri = uri;
        }
    }

    private static final class PlaybackRequest {
        final List<MmfEntry> queue;
        final int startIndex;

        PlaybackRequest(List<MmfEntry> queue, int startIndex) {
            this.queue = new ArrayList<>(queue);
            this.startIndex = startIndex;
        }
    }

    private static final class FolderLocation {
        final String documentId;
        final String displayPath;

        FolderLocation(String documentId, String displayPath) {
            this.documentId = documentId;
            this.displayPath = displayPath;
        }
    }

    private static final class BrowserEntry {
        static final int DIRECTORY = 1;
        static final int FILE = 2;

        final int kind;
        final String name;
        final String documentId;
        final MmfEntry file;

        private BrowserEntry(int kind, String name, String documentId, MmfEntry file) {
            this.kind = kind;
            this.name = name;
            this.documentId = documentId;
            this.file = file;
        }

        static BrowserEntry directory(String name, String documentId) {
            return new BrowserEntry(DIRECTORY, name, documentId, null);
        }

        static BrowserEntry file(MmfEntry file) {
            return new BrowserEntry(FILE, file.name, null, file);
        }
    }
}
