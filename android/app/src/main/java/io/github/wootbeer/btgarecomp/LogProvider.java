package io.github.wootbeer.btgarecomp;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * Lets the app a player shares the log with read it (LogFiles.shareIntent): read-only, only the
 * files in the shared-logs folder of the app's cache, and only for the app the share went to
 * (not exported; access comes from the share's read grant).
 */
public class LogProvider extends ContentProvider {
    private static final String FOLDER = "shared_logs";

    static File folder(Context context) {
        return new File(context.getCacheDir(), FOLDER);
    }

    static Uri uriFor(Context context, File file) {
        return new Uri.Builder()
                .scheme("content")
                .authority(context.getPackageName() + ".logs")
                .appendPath(file.getName())
                .build();
    }

    /** The file a URI names, only ever one directly in the shared-logs folder. */
    private File fileFor(Uri uri) throws FileNotFoundException {
        String name = uri.getLastPathSegment();
        if (name == null || uri.getPathSegments().size() != 1 || name.contains("/") || name.startsWith(".")) {
            throw new FileNotFoundException(uri.toString());
        }
        File file = new File(folder(getContext()), name);
        if (!file.isFile()) {
            throw new FileNotFoundException(uri.toString());
        }
        return file;
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) {
            throw new FileNotFoundException("Read-only: " + uri);
        }
        return ParcelFileDescriptor.open(fileFor(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        File file;
        try {
            file = fileFor(uri);
        } catch (FileNotFoundException e) {
            return null;
        }
        if (projection == null) {
            projection = new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE };
        }
        MatrixCursor cursor = new MatrixCursor(projection, 1);
        Object[] row = new Object[projection.length];
        for (int i = 0; i < projection.length; i++) {
            if (OpenableColumns.DISPLAY_NAME.equals(projection[i])) {
                row[i] = file.getName();
            } else if (OpenableColumns.SIZE.equals(projection[i])) {
                row[i] = file.length();
            }
        }
        cursor.addRow(row);
        return cursor;
    }

    @Override
    public String getType(Uri uri) {
        return "text/plain";
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException("Read-only");
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("Read-only");
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("Read-only");
    }
}
