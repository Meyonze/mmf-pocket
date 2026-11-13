package app.mmfpocket.player;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

/** Host checks; compile/run with an org.json JVM jar on the classpath. */
public final class UpdateCheckerTest {
    private static void check(boolean value) {
        if (!value) throw new AssertionError();
    }

    private static String release(String tag, String url, boolean draft, boolean prerelease) {
        return "{\"tag_name\":\"" + tag + "\",\"html_url\":\"" + url
                + "\",\"draft\":" + draft + ",\"prerelease\":" + prerelease + "}";
    }

    private static void reject(String text) throws Exception {
        try {
            UpdateChecker.parseRelease(text);
            throw new AssertionError("Accepted invalid release");
        } catch (IOException expected) {
            // Invalid, preview, or unrelated releases must never open a browser.
        }
    }

    public static void main(String[] args) throws Exception {
        check(UpdateChecker.isNewer("1.6.10", "1.6.9"));
        check(UpdateChecker.isNewer("1.10.0", "1.9.99"));
        check(UpdateChecker.isNewer("2.0.0", "1.99.99"));
        check(!UpdateChecker.isNewer("1.6.8", "1.6.8"));
        check(!UpdateChecker.isNewer("1.6.7", "1.6.8"));
        check(!UpdateChecker.isNewer("1.9.99", "1.10.0"));
        for (String invalid : new String[] {"", "1.6", "v1.6.8", "1.6.9-beta",
                "-1.6.8", "99999999999999.0.0", "1.6.8.1"}) {
            try {
                UpdateChecker.isNewer(invalid, "1.6.8");
                throw new AssertionError("Accepted invalid version");
            } catch (IllegalArgumentException expected) {
                // Stable numeric versions only.
            }
        }
        String url = "https://github.com/Meyonze/mmf-pocket/releases/tag/v1.6.10";
        UpdateChecker.Release latest = UpdateChecker.parseRelease(
                release("v1.6.10", url, false, false));
        check(latest.version.equals("1.6.10") && latest.pageUrl.equals(url));
        reject(release("v1.6.10", url, true, false));
        reject(release("v1.6.10", url, false, true));
        reject(release("v1.6.10-beta", url, false, false));
        reject(release("v1.6.10", "https://example.com/", false, false));
        reject(release("v1.6.10", url + "/redirect", false, false));
        reject(release("v1.6.10", "http://github.com/Meyonze/mmf-pocket/releases/tag/v1.6.10", false, false));
        reject("{}");
        reject("not json");
        if (args.length > 0) {
            UpdateChecker.Release actual = UpdateChecker.parseRelease(Files.readString(Path.of(args[0])));
            check(actual.version.equals("1.6.8"));
            check(!UpdateChecker.isNewer(actual.version, "1.6.8"));
        }
        System.out.println("UpdateChecker checks passed");
    }
}
