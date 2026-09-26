package app.mmfpocket.player;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.media.MediaPlayer;
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

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

public final class MainActivity extends Activity {
    private static final int REQUEST_FOLDER = 1001;
    private static final String PREFS = "mmf_player";
    private static final String PREF_TREE_URI = "tree_uri";
    private static final String PREF_PHONE_SOUND = "phone_sound";
    // Bump whenever synthesis, timing or post-processing changes so an older
    // WAV cannot hide a renderer fix behind a valid content hash.
    private static final String RENDER_CACHE_VERSION = "r6";
    // Ringtones are normally tiny. Keeping a conservative ceiling limits memory
    // amplification in the native decoder when opening an untrusted file.
    private static final int MAX_MMF_BYTES = 16 * 1024 * 1024;
    private static final long MAX_CACHE_BYTES = 512L * 1024 * 1024;
    private static final String CACHE_DIR_NAME = "converted";

    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final AtomicInteger playbackGeneration = new AtomicInteger();
    private final AtomicInteger scanGeneration = new AtomicInteger();
    private final AtomicInteger batchGeneration = new AtomicInteger();
    private final Handler progressHandler = new Handler(Looper.getMainLooper());
    private final Runnable progressUpdater = new Runnable() {
        @Override
        public void run() {
            updatePlayerProgress();
            boolean keepUpdating = progressivePlayback != null
                    && (!progressivePlayback.isRenderingComplete()
                    || progressivePlayback.isPlaying());
            if (keepUpdating || prepared && mediaPlayer != null && mediaPlayer.isPlaying()) {
                progressHandler.postDelayed(this, 250);
            }
        }
    };
    private final List<BrowserEntry> browserEntries = new ArrayList<>();
    private final List<MmfEntry> entries = new ArrayList<>();
    private final List<String> names = new ArrayList<>();
    private final ArrayDeque<FolderLocation> folderHistory = new ArrayDeque<>();

    private ArrayAdapter<String> adapter;
    private TextView folderLabel;
    private TextView statusLabel;
    private TextView formatLabel;
    private TextView nowPlayingLabel;
    private TextView timeLabel;
    private ListView listView;
    private SeekBar playbackProgress;
    private Button chooseButton;
    private Button batchButton;
    private Switch phoneSoundSwitch;
    private ImageButton playPauseButton;
    private ImageButton stopButton;
    private MediaPlayer mediaPlayer;
    private ProgressivePlayback progressivePlayback;
    private File currentWav;
    private String currentName;
    // The selected source survives stop/mode changes; a prepared WAV does not.
    private MmfEntry selectedEntry;
    private boolean prepared;
    private boolean batchConverting;
    private Uri selectedTreeUri;
    private FolderLocation currentFolder;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(buildUi());
        cleanupStaleWavFiles();

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

        TextView title = new TextView(this);
        title.setText(getString(R.string.app_name));
        title.setTextSize(24);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        root.addView(title, new LinearLayout.LayoutParams(
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
            stopPlayback(false);
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
                if (!fromUser || !prepared) return;
                if (progressivePlayback != null) {
                    long totalFrames = progressivePlayback.getTimelineFrames();
                    long requested = totalFrames * progress / seekBar.getMax();
                    long available = progressivePlayback.getRenderedFrames();
                    long preview = Math.min(requested, available);
                    timeLabel.setText(getString(R.string.playback_time,
                            formatFrames(preview), formatFrames(totalFrames)));
                } else if (mediaPlayer != null) {
                    int duration = mediaPlayer.getDuration();
                    int position = (int) ((long) duration * progress / seekBar.getMax());
                    timeLabel.setText(getString(R.string.playback_time,
                            formatTime(position), formatTime(duration)));
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
                progressHandler.removeCallbacks(progressUpdater);
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                if (prepared && progressivePlayback != null) {
                    long totalFrames = progressivePlayback.getTimelineFrames();
                    long requested = totalFrames * seekBar.getProgress() / seekBar.getMax();
                    long actual = progressivePlayback.seekTo(requested);
                    seekBar.setProgress((int) (actual * seekBar.getMax()
                            / Math.max(1, totalFrames)));
                    startProgressUpdates();
                } else if (prepared && mediaPlayer != null) {
                    int duration = mediaPlayer.getDuration();
                    mediaPlayer.seekTo((int) ((long) duration
                            * seekBar.getProgress() / seekBar.getMax()));
                    if (mediaPlayer.isPlaying()) startProgressUpdates();
                    else updatePlayerProgress();
                }
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
        cancelBatchConversion();
        selectedEntry = null;
        currentName = null;
        stopPlayback(false);
        nowPlayingLabel.setText(R.string.no_track_selected);
        formatLabel.setText("");
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
        cancelBatchConversion();
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
                batchButton.setEnabled(!entries.isEmpty());
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
        if (batchConverting) return;
        selectedEntry = entry;
        boolean phoneSpeakerMode = phoneSoundSwitch.isChecked();
        int generation = playbackGeneration.incrementAndGet();
        releasePlayer();
        currentName = entry.name;
        nowPlayingLabel.setText(entry.name);
        formatLabel.setText("");
        prepared = false;
        playPauseButton.setEnabled(false);
        stopButton.setEnabled(true);
        statusLabel.setText(getString(R.string.converting_file, entry.name));

        worker.execute(() -> {
            PlaybackSource source;
            try {
                source = preparePlaybackSource(entry, phoneSpeakerMode);
            } catch (Exception exception) {
                String message = exception.getMessage() == null
                        ? exception.getClass().getSimpleName() : exception.getMessage();
                source = PlaybackSource.error(message);
            }

            PlaybackSource finalSource = source;
            runOnUiThread(() -> {
                if (generation != playbackGeneration.get() || isFinishing() || isDestroyed()) {
                    return;
                }
                if (!finalSource.error.isEmpty()) {
                    statusLabel.setText(getString(R.string.playback_failed, finalSource.error));
                    playPauseButton.setEnabled(selectedEntry != null && !batchConverting);
                    stopButton.setEnabled(false);
                    return;
                }
                formatLabel.setText(finalSource.formatLabel);
                if (finalSource.cached != null) {
                    startMediaPlayer(finalSource.cached, entry.name);
                } else {
                    startProgressivePlayback(finalSource, entry.name, generation);
                }
            });
        });
    }

    private void convertAllFiles() {
        if (entries.isEmpty() || batchConverting) return;
        stopPlayback(false);
        int generation = batchGeneration.incrementAndGet();
        List<MmfEntry> snapshot = new ArrayList<>(entries);
        boolean phoneSpeakerMode = phoneSoundSwitch.isChecked();
        batchConverting = true;
        playPauseButton.setEnabled(false);
        phoneSoundSwitch.setEnabled(false);
        chooseButton.setEnabled(false);
        batchButton.setEnabled(false);
        listView.setEnabled(false);

        worker.execute(() -> {
            int converted = 0;
            int reused = 0;
            int failed = 0;
            for (int i = 0; i < snapshot.size(); i++) {
                if (generation != batchGeneration.get() || Thread.currentThread().isInterrupted()) return;
                MmfEntry entry = snapshot.get(i);
                int position = i + 1;
                runOnUiThread(() -> {
                    if (generation == batchGeneration.get()) {
                        statusLabel.setText(getString(R.string.batch_progress,
                                position, snapshot.size(), entry.name));
                    }
                });
                try {
                    RenderResult result = renderOrGetCached(entry, phoneSpeakerMode);
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
            }

            int finalConverted = converted;
            int finalReused = reused;
            int finalFailed = failed;
            runOnUiThread(() -> {
                if (generation != batchGeneration.get() || isFinishing() || isDestroyed()) return;
                batchConverting = false;
                phoneSoundSwitch.setEnabled(true);
                playPauseButton.setEnabled(selectedEntry != null);
                chooseButton.setEnabled(true);
                batchButton.setEnabled(!entries.isEmpty());
                listView.setEnabled(true);
                statusLabel.setText(getString(R.string.batch_complete,
                        finalConverted, finalReused, finalFailed));
            });
        });
    }

    private void cancelBatchConversion() {
        batchGeneration.incrementAndGet();
        if (!batchConverting) return;
        batchConverting = false;
        phoneSoundSwitch.setEnabled(true);
        playPauseButton.setEnabled(selectedEntry != null);
        chooseButton.setEnabled(true);
        batchButton.setEnabled(!entries.isEmpty());
        listView.setEnabled(true);
    }

    private PlaybackSource preparePlaybackSource(
            MmfEntry entry, boolean phoneSpeakerMode) throws IOException {
        byte[] bytes = readMmf(entry.uri);
        String formatLabel = NativeMmfRenderer.detectFormat(bytes);
        File cacheDirectory = new File(getCacheDir(), CACHE_DIR_NAME);
        if (!cacheDirectory.isDirectory() && !cacheDirectory.mkdirs()) {
            throw new IOException(getString(R.string.cache_create_failed));
        }

        String key = RENDER_CACHE_VERSION + "-" + sha256(bytes);
        File cached = new File(cacheDirectory,
                key + (phoneSpeakerMode ? "-phone.wav" : ".wav"));
        if (cached.isFile() && cached.length() > 44) {
            //noinspection ResultOfMethodCallIgnored
            cached.setLastModified(System.currentTimeMillis());
            return new PlaybackSource(
                    bytes, cached, cached, null, phoneSpeakerMode, formatLabel, "");
        }

        File partial = new File(cacheDirectory, key + "-" + System.nanoTime() + ".part");
        return new PlaybackSource(
                bytes, null, cached, partial, phoneSpeakerMode, formatLabel, "");
    }

    private RenderResult renderOrGetCached(MmfEntry entry, boolean phoneSpeakerMode) throws IOException {
        PlaybackSource source = preparePlaybackSource(entry, phoneSpeakerMode);
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

    private String sha256(byte[] bytes) {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(bytes);
            StringBuilder result = new StringBuilder(digest.length * 2);
            for (byte value : digest) result.append(String.format(Locale.ROOT, "%02x", value & 0xff));
            return result.toString();
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException(impossible);
        }
    }

    private void trimCache(File cacheDirectory, File protectedFile) {
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

    private byte[] readMmf(Uri uri) throws IOException {
        ContentResolver resolver = getContentResolver();
        try (InputStream input = resolver.openInputStream(uri);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            if (input == null) throw new IOException(getString(R.string.file_open_failed));
            byte[] buffer = new byte[32 * 1024];
            int total = 0;
            int count;
            while ((count = input.read(buffer)) >= 0) {
                total += count;
                if (total > MAX_MMF_BYTES) {
                    throw new IOException(getString(R.string.file_too_large));
                }
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        }
    }

    private void startProgressivePlayback(
            PlaybackSource source, String name, int generation) {
        statusLabel.setText(getString(R.string.buffering_file, name));
        playbackProgress.setProgress(0);
        playbackProgress.setSecondaryProgress(0);
        playbackProgress.setEnabled(false);
        playPauseButton.setEnabled(false);
        stopButton.setEnabled(true);

        ProgressivePlayback playback = new ProgressivePlayback(
                source.bytes, source.phoneSpeakerMode, source.partial, source.target,
                new ProgressivePlayback.Listener() {
                    @Override
                    public void onReady() {
                        runOnUiThread(() -> {
                            if (generation != playbackGeneration.get()
                                    || isFinishing() || isDestroyed()) return;
                            prepared = true;
                            playbackProgress.setEnabled(true);
                            playPauseButton.setEnabled(true);
                            stopButton.setEnabled(true);
                            showPauseIcon();
                            statusLabel.setText(getString(R.string.playing_file, name));
                            startProgressUpdates();
                        });
                    }

                    @Override
                    public void onCacheReady(File cached) {
                        trimCache(cached.getParentFile(), cached);
                        runOnUiThread(() -> {
                            if (generation == playbackGeneration.get()) currentWav = cached;
                        });
                    }

                    @Override
                    public void onCompleted() {
                        runOnUiThread(() -> {
                            if (generation != playbackGeneration.get()
                                    || isFinishing() || isDestroyed()) return;
                            updatePlayerProgress();
                            showPlayIcon();
                            statusLabel.setText(getString(R.string.playback_finished, name));
                        });
                    }

                    @Override
                    public void onError(String message) {
                        runOnUiThread(() -> {
                            if (generation != playbackGeneration.get()
                                    || isFinishing() || isDestroyed()) return;
                            releasePlayer();
                            statusLabel.setText(getString(R.string.playback_failed, message));
                        });
                    }
                });
        progressivePlayback = playback;
        playback.start();
        startProgressUpdates();
    }

    private void startMediaPlayer(File wav, String name) {
        releasePlayer();
        playPauseButton.setEnabled(false);
        currentWav = wav;
        mediaPlayer = new MediaPlayer();
        try {
            mediaPlayer.setDataSource(wav.getAbsolutePath());
            mediaPlayer.setOnPreparedListener(player -> {
                if (player != mediaPlayer) return;
                prepared = true;
                player.start();
                playbackProgress.setEnabled(true);
                playbackProgress.setSecondaryProgress(playbackProgress.getMax());
                playPauseButton.setEnabled(true);
                showPauseIcon();
                stopButton.setEnabled(true);
                statusLabel.setText(getString(R.string.playing_file, name));
                startProgressUpdates();
            });
            mediaPlayer.setOnCompletionListener(player -> {
                if (player != mediaPlayer) return;
                progressHandler.removeCallbacks(progressUpdater);
                updatePlayerProgress();
                showPlayIcon();
                statusLabel.setText(getString(R.string.playback_finished, name));
            });
            mediaPlayer.setOnErrorListener((player, what, extra) -> {
                if (player != mediaPlayer) return true;
                statusLabel.setText(R.string.audio_playback_failed);
                releasePlayer();
                return true;
            });
            mediaPlayer.prepareAsync();
        } catch (IOException error) {
            statusLabel.setText(getString(R.string.audio_open_failed, error.getMessage()));
            releasePlayer();
        }
    }

    private void togglePlayback() {
        if (batchConverting) return;
        if (progressivePlayback != null) {
            if (!prepared) return;
            if (progressivePlayback.isPlaybackComplete()) {
                File replay = currentWav;
                if (replay != null && replay.isFile()) {
                    releasePlayer();
                    startMediaPlayer(replay, currentName);
                } else if (selectedEntry != null) {
                    play(selectedEntry);
                }
            } else if (progressivePlayback.isPlaying()) {
                progressivePlayback.pause();
                updatePlayerProgress();
                showPlayIcon();
                statusLabel.setText(getString(R.string.paused_file, currentName));
            } else {
                progressivePlayback.resume();
                startProgressUpdates();
                showPauseIcon();
                statusLabel.setText(getString(R.string.playing_file, currentName));
            }
            return;
        }
        if (!prepared || mediaPlayer == null) {
            if (selectedEntry != null) play(selectedEntry);
            return;
        }
        if (mediaPlayer.isPlaying()) {
            mediaPlayer.pause();
            progressHandler.removeCallbacks(progressUpdater);
            updatePlayerProgress();
            showPlayIcon();
            statusLabel.setText(getString(R.string.paused_file, currentName));
        } else {
            if (mediaPlayer.getCurrentPosition() >= mediaPlayer.getDuration() - 100) {
                mediaPlayer.seekTo(0);
            }
            mediaPlayer.start();
            startProgressUpdates();
            showPauseIcon();
            statusLabel.setText(getString(R.string.playing_file, currentName));
        }
    }

    private void stopPlayback(boolean updateStatus) {
        playbackGeneration.incrementAndGet();
        releasePlayer();
        if (updateStatus) statusLabel.setText(R.string.playback_stopped);
    }

    private void releasePlayer() {
        progressHandler.removeCallbacks(progressUpdater);
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
        if (currentWav != null) {
            currentWav = null;
        }
        showPlayIcon();
        playPauseButton.setEnabled(selectedEntry != null && !batchConverting);
        stopButton.setEnabled(false);
        playbackProgress.setProgress(0);
        playbackProgress.setSecondaryProgress(0);
        playbackProgress.setEnabled(false);
        timeLabel.setText(R.string.zero_playback_time);
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
        if (progressivePlayback != null) {
            long total = progressivePlayback.getTimelineFrames();
            long position = progressivePlayback.getPositionFrames();
            long rendered = progressivePlayback.getRenderedFrames();
            int max = playbackProgress.getMax();
            playbackProgress.setProgress((int) (Math.min(position, total) * max
                    / Math.max(1, total)));
            playbackProgress.setSecondaryProgress((int) (Math.min(rendered, total) * max
                    / Math.max(1, total)));
            timeLabel.setText(getString(R.string.playback_time,
                    formatFrames(position), formatFrames(total)));
            return;
        }
        if (!prepared || mediaPlayer == null) return;
        int duration = Math.max(0, mediaPlayer.getDuration());
        int position = Math.max(0, mediaPlayer.getCurrentPosition());
        int progress = duration == 0 ? 0
                : (int) ((long) position * playbackProgress.getMax() / duration);
        playbackProgress.setProgress(progress);
        playbackProgress.setSecondaryProgress(playbackProgress.getMax());
        timeLabel.setText(getString(R.string.playback_time,
                formatTime(position), formatTime(duration)));
    }

    private String formatFrames(long frames) {
        return formatTime((int) Math.min(Integer.MAX_VALUE,
                frames * 1000 / ProgressivePlayback.SAMPLE_RATE));
    }

    private String formatTime(int milliseconds) {
        int totalSeconds = Math.max(0, milliseconds) / 1000;
        return String.format(Locale.ROOT, "%d:%02d", totalSeconds / 60, totalSeconds % 60);
    }

    @Override
    protected void onStop() {
        cancelBatchConversion();
        stopPlayback(true);
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
        playbackGeneration.incrementAndGet();
        scanGeneration.incrementAndGet();
        batchGeneration.incrementAndGet();
        progressHandler.removeCallbacks(progressUpdater);
        releasePlayer();
        worker.shutdownNow();
        super.onDestroy();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void cleanupStaleWavFiles() {
        File[] files = getCacheDir().listFiles((directory, name) ->
                name.startsWith("playback-") && name.endsWith(".wav"));
        if (files == null) return;
        for (File file : files) {
            //noinspection ResultOfMethodCallIgnored
            file.delete();
        }
        File cacheDirectory = new File(getCacheDir(), CACHE_DIR_NAME);
        File[] partials = cacheDirectory.listFiles((directory, name) -> name.endsWith(".part"));
        if (partials == null) return;
        for (File partial : partials) {
            //noinspection ResultOfMethodCallIgnored
            partial.delete();
        }
    }

    private static final class RenderResult {
        final File file;
        final String error;
        final boolean cacheHit;

        RenderResult(File file, String error, boolean cacheHit) {
            this.file = file;
            this.error = error;
            this.cacheHit = cacheHit;
        }
    }

    private static final class PlaybackSource {
        final byte[] bytes;
        final File cached;
        final File target;
        final File partial;
        final boolean phoneSpeakerMode;
        final String formatLabel;
        final String error;

        PlaybackSource(byte[] bytes, File cached, File target, File partial,
                       boolean phoneSpeakerMode, String formatLabel, String error) {
            this.bytes = bytes;
            this.cached = cached;
            this.target = target;
            this.partial = partial;
            this.phoneSpeakerMode = phoneSpeakerMode;
            this.formatLabel = formatLabel;
            this.error = error;
        }

        static PlaybackSource error(String message) {
            return new PlaybackSource(null, null, null, null, false, "", message);
        }
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
