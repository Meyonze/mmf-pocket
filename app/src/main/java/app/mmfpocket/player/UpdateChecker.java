package app.mmfpocket.player;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InterruptedIOException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/** Explicit, user-initiated checks of this app's stable public GitHub release. */
final class UpdateChecker {
    private static final String API_URL =
            "https://api.github.com/repos/Meyonze/mmf-pocket/releases/latest";
    private static final String RELEASE_URL =
            "https://github.com/Meyonze/mmf-pocket/releases/tag/";
    private static final int MAX_RESPONSE_BYTES = 128 * 1024;

    static final class Release {
        final String version;
        final String pageUrl;

        Release(String version, String pageUrl) {
            this.version = version;
            this.pageUrl = pageUrl;
        }
    }

    static Release checkLatest() throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(API_URL).openConnection();
        try {
            connection.setConnectTimeout(8000);
            connection.setReadTimeout(8000);
            connection.setInstanceFollowRedirects(false);
            connection.setUseCaches(false);
            connection.setRequestProperty("Accept", "application/vnd.github+json");
            connection.setRequestProperty("User-Agent", "MMF-Pocket-Update-Check");
            if (connection.getResponseCode() != HttpURLConnection.HTTP_OK) {
                throw new IOException("Update service unavailable");
            }
            try (InputStream input = connection.getInputStream();
                    ByteArrayOutputStream output = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[4096];
                int size;
                while ((size = input.read(buffer)) != -1) {
                    if (Thread.currentThread().isInterrupted()) {
                        throw new InterruptedIOException("Update check cancelled");
                    }
                    if (output.size() + size > MAX_RESPONSE_BYTES) {
                        throw new IOException("Update response too large");
                    }
                    output.write(buffer, 0, size);
                }
                return parseRelease(new String(output.toByteArray(), StandardCharsets.UTF_8));
            }
        } finally {
            connection.disconnect();
        }
    }

    static Release parseRelease(String text) throws IOException {
        try {
            JSONObject json = new JSONObject(text);
            if (json.getBoolean("draft") || json.getBoolean("prerelease")) {
                throw new IOException("Not a stable public release");
            }
            String tag = json.getString("tag_name");
            if (!tag.startsWith("v")) throw new IOException("Invalid release tag");
            String version = tag.substring(1);
            components(version);
            String pageUrl = json.getString("html_url");
            if (!pageUrl.equals(RELEASE_URL + tag)) {
                throw new IOException("Unexpected release page");
            }
            return new Release(version, pageUrl);
        } catch (JSONException | IllegalArgumentException exception) {
            throw new IOException("Invalid update response", exception);
        }
    }

    static boolean isNewer(String latest, String installed) {
        int[] next = components(latest);
        int[] current = components(installed);
        for (int i = 0; i < next.length; i++) {
            if (next[i] != current[i]) return next[i] > current[i];
        }
        return false;
    }

    private static int[] components(String version) {
        if (version == null || !version.matches("[0-9]{1,9}\\.[0-9]{1,9}\\.[0-9]{1,9}")) {
            throw new IllegalArgumentException("Invalid stable version");
        }
        String[] parts = version.split("\\.");
        return new int[] {Integer.parseInt(parts[0]), Integer.parseInt(parts[1]),
                Integer.parseInt(parts[2])};
    }
}
