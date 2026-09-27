package app.mmfpocket.player;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
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
import android.view.Gravity;
import android.view.View;
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
import androidx.media3.common.MediaMetadata;
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
            updateUiFromController();
        }
    };
    private final List<BrowserEntry> browserEntries = new ArrayList<>();
    private final List<MmfEntry> entries = new ArrayList<>();
    private final List<String> names = new ArrayList<>();
    private final ArrayDeque<FolderLocation> folderHistory = new ArrayDeque<>();

    private ArrayAdapter<String> adapter;
    private TextView folderLabel;
    private TextView batchStatusLabel;
    private TextView statusLabel;
    private TextView formatLabel;
    private TextView nowPlayingLabel;
    private TextView timeLabel;
    private ListView listView;
    private SeekBar playbackProgress;
    private Button chooseButton;
    private Button batchButton;
    private Button batchPauseButton;
    private Switch phoneSoundSwitch;
    private ImageButton playPauseButton;
    private ImageButton stopButton;
    private String currentName;
    private MmfEntry selectedEntry;
    private MmfEntry pendingPlaybackEntry;
    private boolean pendingPhoneModeUpdate;
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
        int pad = dp(16);
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
        title.setTextSize(24);
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

        root.addView(titleRow, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        LinearLayout folderControls = new LinearLayout(this);
        folderControls.setOrientation(LinearLayout.HORIZONTAL);
        folderControls.setGravity(Gravity.CENTER_VERTICAL | Gravity.END);
        LinearLayout.LayoutParams folderControlsParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        folderControlsParams.topMargin = dp(6);

        folderLabel = new TextView(this);
        folderLabel.setText(R.string.no_folder_ja);
        folderLabel.setTextSize(12);
        folderLabel.setSingleLine(true);
        folderLabel.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        folderLabel.setPadding(0, 0, dp(6), 0);
        folderControls.addView(folderLabel, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        chooseButton = new Button(this);
        chooseButton.setText(R.string.choose_folder_compact);
        chooseButton.setTextSize(12);
        chooseButton.setMinWidth(0);
        chooseButton.setMinimumWidth(0);
        chooseButton.setOnClickListener(v -> chooseFolder());
        folderControls.addView(chooseButton, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        batchButton = new Button(this);
        batchButton.setText(R.string.convert_folder_compact);
        batchButton.setTextSize(12);
        batchButton.setMinWidth(0);
        batchButton.setMinimumWidth(0);
        batchButton.setEnabled(false);
        batchButton.setOnClickListener(v -> convertAllFiles());
        LinearLayout.LayoutParams batchParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        batchParams.leftMargin = dp(4);
        folderControls.addView(batchButton, batchParams);

        batchPauseButton = new Button(this);
        batchPauseButton.setText(R.string.pause_batch_compact);
        batchPauseButton.setTextSize(12);
        batchPauseButton.setMinWidth(0);
        batchPauseButton.setMinimumWidth(0);
        batchPauseButton.setVisibility(View.GONE);
        batchPauseButton.setOnClickListener(v -> toggleBatchPause());
        LinearLayout.LayoutParams batchPauseParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        batchPauseParams.leftMargin = dp(4);
        folderControls.addView(batchPauseButton, batchPauseParams);
        root.addView(folderControls, folderControlsParams);

        listView = new ListView(this);
        listView.setChoiceMode(ListView.CHOICE_MODE_SINGLE);
        adapter = new ArrayAdapter<>(this, android.R.layout.simple_list_item_activated_1, names);
        listView.setAdapter(adapter);
        listView.setOnItemClickListener((parent, view, position, id) -> {
            BrowserEntry entry = browserEntries.get(position);
            if (entry.kind == BrowserEntry.PARENT) {
                navigateUp();
            } else if (entry.kind == BrowserEntry.DIRECTORY) {
                navigateInto(entry);
            } else {
                play(entry.file);
            }
        });
        root.addView(listView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));

        LinearLayout playerPanel = new LinearLayout(this);
        playerPanel.setOrientation(LinearLayout.VERTICAL);
        playerPanel.setPadding(dp(12), dp(10), dp(12), dp(10));
        playerPanel.setBackgroundResource(R.drawable.player_panel);
        LinearLayout.LayoutParams playerParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        playerParams.topMargin = dp(8);

        LinearLayout playerTop = new LinearLayout(this);
        playerTop.setOrientation(LinearLayout.HORIZONTAL);
        playerTop.setGravity(Gravity.CENTER_VERTICAL);

        nowPlayingLabel = new TextView(this);
        nowPlayingLabel.setText(R.string.no_track_selected);
        nowPlayingLabel.setTextSize(16);
        nowPlayingLabel.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        nowPlayingLabel.setSingleLine(true);
        nowPlayingLabel.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        playerTop.addView(nowPlayingLabel, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        phoneSoundSwitch = new Switch(this);
        phoneSoundSwitch.setText(R.string.phone_sound_mode_compact);
        phoneSoundSwitch.setTextSize(12);
        phoneSoundSwitch.setChecked(getSharedPreferences(PREFS, MODE_PRIVATE)
                .getBoolean(PREF_PHONE_SOUND, false));
        phoneSoundSwitch.setOnCheckedChangeListener((button, checked) -> {
            getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                    .putBoolean(PREF_PHONE_SOUND, checked).apply();
            updatePhoneSoundMode(checked);
            statusLabel.setText(checked ? R.string.phone_mode_enabled : R.string.phone_mode_disabled);
        });
        playerTop.addView(phoneSoundSwitch);
        playerPanel.addView(playerTop);

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
                controller.seekTo(Math.min(requested, controller.getBufferedPosition()));
                if (controller.isPlaying()) startProgressUpdates();
                else updatePlayerProgress();
            }
        });
        playerPanel.addView(playbackProgress);

        timeLabel = new TextView(this);
        timeLabel.setText(R.string.zero_playback_time);
        timeLabel.setTextSize(12);
        timeLabel.setGravity(Gravity.END);
        playerPanel.addView(timeLabel);

        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.HORIZONTAL);
        controls.setGravity(Gravity.CENTER);

        playPauseButton = new ImageButton(this);
        playPauseButton.setImageResource(R.drawable.ic_play);
        playPauseButton.setContentDescription(getString(R.string.play_description));
        playPauseButton.setBackgroundResource(R.drawable.control_button_background);
        playPauseButton.setPadding(dp(13), dp(13), dp(13), dp(13));
        playPauseButton.setEnabled(false);
        playPauseButton.setOnClickListener(v -> togglePlayback());
        controls.addView(playPauseButton, new LinearLayout.LayoutParams(dp(52), dp(52)));

        stopButton = new ImageButton(this);
        stopButton.setImageResource(R.drawable.ic_stop);
        stopButton.setContentDescription(getString(R.string.stop_description));
        stopButton.setBackgroundResource(R.drawable.control_button_background);
        stopButton.setPadding(dp(14), dp(14), dp(14), dp(14));
        stopButton.setEnabled(false);
        stopButton.setOnClickListener(v -> stopPlayback(true));
        LinearLayout.LayoutParams stopParams = new LinearLayout.LayoutParams(dp(52), dp(52));
        stopParams.leftMargin = dp(16);
        controls.addView(stopButton, stopParams);
        playerPanel.addView(controls);

        LinearLayout playerFooter = new LinearLayout(this);
        playerFooter.setOrientation(LinearLayout.HORIZONTAL);
        playerFooter.setGravity(Gravity.CENTER_VERTICAL);
        playerFooter.setPadding(0, dp(6), 0, 0);

        statusLabel = new TextView(this);
        statusLabel.setText(R.string.select_mmf_prompt);
        statusLabel.setTextSize(12);
        statusLabel.setSingleLine(true);
        statusLabel.setEllipsize(TextUtils.TruncateAt.END);
        playerFooter.addView(statusLabel, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        formatLabel = new TextView(this);
        formatLabel.setTextSize(12);
        formatLabel.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        formatLabel.setGravity(Gravity.END);
        formatLabel.setSingleLine(true);
        formatLabel.setPadding(dp(10), 0, 0, 0);
        playerFooter.addView(formatLabel, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        playerPanel.addView(playerFooter);
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
                if (!folderHistory.isEmpty()) {
                    browserEntries.add(BrowserEntry.parent());
                    names.add(getString(R.string.parent_folder));
                }
                int folderCount = 0;
                for (BrowserEntry entry : found) {
                    browserEntries.add(entry);
                    if (entry.kind == BrowserEntry.DIRECTORY) {
                        folderCount++;
                        names.add(getString(R.string.folder_list_item, entry.name));
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
        selectedEntry = entry;
        currentName = entry.name;
        nowPlayingLabel.setText(entry.name);
        formatLabel.setText("");
        playPauseButton.setEnabled(true);
        stopButton.setEnabled(true);
        statusLabel.setText(getString(R.string.converting_file, entry.name));
        MediaController controller = mediaController;
        if (controller == null) {
            pendingPlaybackEntry = entry;
            connectController();
            return;
        }
        startPlayback(controller, entry);
    }

    private void startPlayback(MediaController controller, MmfEntry entry) {
        pendingPlaybackEntry = null;
        pendingPhoneModeUpdate = false;
        Bundle extras = new Bundle();
        extras.putBoolean(MmfPlayer.EXTRA_PHONE_SPEAKER, phoneSoundSwitch.isChecked());
        MediaMetadata metadata = new MediaMetadata.Builder()
                .setTitle(entry.name)
                .setDisplayTitle(entry.name)
                .setExtras(extras)
                .build();
        MediaItem item = new MediaItem.Builder()
                .setMediaId(entry.uri.toString())
                .setUri(entry.uri)
                .setMimeType("audio/x-smaf")
                .setMediaMetadata(metadata)
                .build();
        controller.setMediaItem(item);
        controller.prepare();
        controller.play();
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
                MmfEntry pending = pendingPlaybackEntry;
                if (pending != null) {
                    startPlayback(controller, pending);
                } else if (pendingPhoneModeUpdate) {
                    updatePhoneSoundMode(phoneSoundSwitch.isChecked());
                }
            } catch (Exception error) {
                if (controllerFuture == future) controllerFuture = null;
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
            name = item.mediaMetadata.title.toString();
            currentName = name;
            nowPlayingLabel.setText(name);
            Bundle extras = item.mediaMetadata.extras;
            formatLabel.setText(extras == null ? ""
                    : extras.getString(MmfPlayer.EXTRA_FORMAT_LABEL, ""));
        }

        int state = controller.getPlaybackState();
        boolean hasItem = item != null;
        playPauseButton.setEnabled(hasItem || selectedEntry != null);
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
            statusLabel.setText(getString(R.string.converting_file, name));
        } else if (state == Player.STATE_READY && name != null) {
            if (controller.isPlaying()) {
                statusLabel.setText(getString(R.string.playing_file, name));
            } else if (controller.getPlaybackSuppressionReason()
                    == Player.PLAYBACK_SUPPRESSION_REASON_TRANSIENT_AUDIO_FOCUS_LOSS) {
                statusLabel.setText(getString(R.string.audio_focus_paused, name));
            } else {
                statusLabel.setText(getString(R.string.paused_file, name));
            }
        } else if (state == Player.STATE_ENDED && name != null) {
            statusLabel.setText(getString(R.string.playback_finished, name));
        } else if (state == Player.STATE_IDLE && hasItem) {
            statusLabel.setText(R.string.playback_stopped);
        }

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

    private void stopPlayback(boolean updateStatus) {
        MediaController controller = mediaController;
        if (controller != null) controller.stop();
        progressHandler.removeCallbacks(progressUpdater);
        showPlayIcon();
        stopButton.setEnabled(false);
        playbackProgress.setProgress(0);
        playbackProgress.setSecondaryProgress(0);
        playbackProgress.setEnabled(false);
        timeLabel.setText(R.string.zero_playback_time);
        if (updateStatus) statusLabel.setText(R.string.playback_stopped);
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
        MediaItem item = controller.getCurrentMediaItem();
        stopPlayback(false);
        if (item == null) return;

        Bundle extras = item.mediaMetadata.extras == null
                ? new Bundle() : new Bundle(item.mediaMetadata.extras);
        extras.putBoolean(MmfPlayer.EXTRA_PHONE_SPEAKER, enabled);
        MediaMetadata metadata = new MediaMetadata.Builder()
                .populate(item.mediaMetadata)
                .setExtras(extras)
                .build();
        MediaItem.Builder itemBuilder = item.buildUpon().setMediaMetadata(metadata);
        // A MediaItem returned through a remote MediaController intentionally
        // omits localConfiguration, so restore the source URI from our mediaId.
        if (item.localConfiguration == null && !item.mediaId.isEmpty()) {
            itemBuilder.setUri(Uri.parse(item.mediaId)).setMimeType("audio/x-smaf");
        }
        controller.setMediaItem(itemBuilder.build());
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
        playbackProgress.setProgress((int) (Math.min(position, duration) * max / duration));
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

    private static final class FolderLocation {
        final String documentId;
        final String displayPath;

        FolderLocation(String documentId, String displayPath) {
            this.documentId = documentId;
            this.displayPath = displayPath;
        }
    }

    private static final class BrowserEntry {
        static final int PARENT = 0;
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

        static BrowserEntry parent() {
            return new BrowserEntry(PARENT, "", null, null);
        }

        static BrowserEntry directory(String name, String documentId) {
            return new BrowserEntry(DIRECTORY, name, documentId, null);
        }

        static BrowserEntry file(MmfEntry file) {
            return new BrowserEntry(FILE, file.name, null, file);
        }
    }
}
