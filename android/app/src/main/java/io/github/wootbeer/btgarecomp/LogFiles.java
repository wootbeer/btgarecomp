package io.github.wootbeer.btgarecomp;

import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.net.Uri;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * The game's logs, for players to send with a bug report. In the app's private storage (the
 * game's working directory): log.txt (everything the game prints, src/android/android_startup.cpp),
 * crash_log.txt (src/main/crash_handler.cpp) and the same two from the run before. A marker file
 * exists while the game is in the foreground; the game removes it when it quits, so finding it at
 * the next start means the last run ended in a crash (or was killed while in use).
 */
final class LogFiles {
    private static final String TAG = "BTGA";

    static final String LOG = "log.txt";
    static final String CRASH_LOG = "crash_log.txt";
    private static final String PREVIOUS_LOG = "log_previous.txt";
    private static final String PREVIOUS_CRASH_LOG = "crash_log_previous.txt";
    /** Also removed by the game when it quits (btga::android::startup, src/android/android_startup.cpp). */
    private static final String RUNNING_MARKER = "running";

    /** Only so much of each log goes in the shared file: its end, where the trouble is. */
    private static final int MAX_LOG_BYTES = 1024 * 1024;

    private LogFiles() {
    }

    /**
     * At the start of a run, before the game writes anything: the last run's logs become the
     * previous run's. Returns whether the last run ended without quitting.
     */
    static boolean startRun(Context context) {
        File dir = context.getFilesDir();
        boolean crashed = new File(dir, RUNNING_MARKER).exists();
        rotate(new File(dir, LOG), new File(dir, PREVIOUS_LOG));
        rotate(new File(dir, CRASH_LOG), new File(dir, PREVIOUS_CRASH_LOG));
        new File(dir, RUNNING_MARKER).delete();
        return crashed;
    }

    private static void rotate(File current, File previous) {
        previous.delete();
        if (current.exists() && !current.renameTo(previous)) {
            current.delete();
        }
    }

    /** The game is in the foreground: a crash from here on leaves the marker behind. */
    static void markRunning(Context context) {
        try {
            new File(context.getFilesDir(), RUNNING_MARKER).createNewFile();
        } catch (IOException e) {
            Log.w(TAG, "Couldn't create the running marker", e);
        }
    }

    /** In the background: being closed from there (recent apps, low memory) isn't a crash. */
    static void markNotRunning(Context context) {
        new File(context.getFilesDir(), RUNNING_MARKER).delete();
    }

    /**
     * A share sheet for one text file holding the device, the app's version and both runs' logs,
     * or null if it couldn't be written.
     */
    static Intent shareIntent(Context context) {
        File file;
        try {
            file = writeReport(context);
        } catch (IOException e) {
            Log.e(TAG, "Couldn't write the log for sharing", e);
            return null;
        }
        Uri uri = LogProvider.uriFor(context, file);
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType("text/plain");
        send.putExtra(Intent.EXTRA_STREAM, uri);
        send.putExtra(Intent.EXTRA_SUBJECT, "BattleTanx: Global Assault Recompiled log");
        // Some share targets read the file through the clip data, so it carries the grant too.
        send.setClipData(ClipData.newRawUri(file.getName(), uri));
        send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        Intent chooser = Intent.createChooser(send, "Share the game's log");
        chooser.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        return chooser;
    }

    private static File writeReport(Context context) throws IOException {
        File folder = LogProvider.folder(context);
        if (!folder.isDirectory() && !folder.mkdirs()) {
            throw new IOException("Couldn't create " + folder);
        }
        File report = new File(folder, "BattleTanxGARecompiled-log.txt");
        File dir = context.getFilesDir();
        try (OutputStream out = new FileOutputStream(report)) {
            write(out, header(context));
            section(out, "Crash report, latest run", new File(dir, CRASH_LOG));
            section(out, "Log, latest run", new File(dir, LOG));
            section(out, "Crash report, run before", new File(dir, PREVIOUS_CRASH_LOG));
            section(out, "Log, run before", new File(dir, PREVIOUS_LOG));
        }
        return report;
    }

    private static String header(Context context) {
        String version = "unknown";
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            version = info.versionName + " (" + info.getLongVersionCode() + ")";
        } catch (Exception e) {
            // Leave it unknown.
        }
        StringBuilder text = new StringBuilder();
        text.append("BattleTanx: Global Assault Recompiled ").append(version).append('\n');
        text.append("Device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
                .append(" (").append(Build.DEVICE).append(")\n");
        if (Build.VERSION.SDK_INT >= 31) {
            text.append("SoC: ").append(Build.SOC_MANUFACTURER).append(' ').append(Build.SOC_MODEL).append('\n');
        }
        text.append("Android ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT).append(")\n");
        text.append("Shared: ").append(timestamp(System.currentTimeMillis())).append('\n');
        return text.toString();
    }

    private static void section(OutputStream out, String title, File file) throws IOException {
        if (!file.exists()) {
            write(out, "\n===== " + title + ": none =====\n");
            return;
        }
        write(out, "\n===== " + title + " (written " + timestamp(file.lastModified()) + ") =====\n");
        try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
            long length = in.length();
            if (length > MAX_LOG_BYTES) {
                write(out, "[... the first " + (length - MAX_LOG_BYTES) + " bytes left out ...]\n");
                in.seek(length - MAX_LOG_BYTES);
            }
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
        }
    }

    private static String timestamp(long millis) {
        return new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT).format(new Date(millis));
    }

    private static void write(OutputStream out, String text) throws IOException {
        out.write(text.getBytes(StandardCharsets.UTF_8));
    }
}
