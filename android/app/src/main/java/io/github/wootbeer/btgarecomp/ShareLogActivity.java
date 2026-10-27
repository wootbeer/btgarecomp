package io.github.wootbeer.btgarecomp;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

/**
 * The app icon's "Share log" shortcut (res/xml/shortcuts.xml): opens the share sheet with the
 * game's logs and closes. It doesn't start the game, so it works when the game can't start.
 */
public class ShareLogActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Intent share = LogFiles.shareIntent(this);
        if (share == null) {
            Toast.makeText(this, "Couldn't collect the game's log", Toast.LENGTH_LONG).show();
        } else {
            try {
                startActivity(share);
            } catch (ActivityNotFoundException e) {
                Log.e("BTGA", "Nothing to share the log with", e);
            }
        }
        finish();
    }
}
