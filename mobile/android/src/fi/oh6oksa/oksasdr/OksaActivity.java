package fi.oh6oksa.oksasdr;

import android.content.Context;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import android.view.accessibility.AccessibilityManager;

import org.qtproject.qt.android.bindings.QtActivity;

// Qt's activity, with Qt's accessibility bridge only while a screen reader
// explores the screen by touch (TalkBack and the like). Other accessibility
// services (password managers, automation apps, vendor assistants) query the
// screen too, and Qt turns its bridge on for any of them, answering each
// query by blocking Android's UI thread on the Qt thread: with a PTT held
// down that froze the app into "not responding" within seconds on a Lenovo
// tablet (Kurt DL5EAG). Qt reads QT_ANDROID_DISABLE_ACCESSIBILITY when the
// accessibility state changes, so the variable is set before Qt starts and
// then follows touch exploration.
public class OksaActivity extends QtActivity {
    private static final String TAG = "OksaSDR";
    private static final String DISABLE = "QT_ANDROID_DISABLE_ACCESSIBILITY";

    @Override
    public void onCreate(Bundle savedInstanceState) {
        AccessibilityManager manager =
            (AccessibilityManager) getSystemService(Context.ACCESSIBILITY_SERVICE);
        if (manager != null) {
            follow(manager.isTouchExplorationEnabled());
            // A screen reader turned on while the app runs: Qt picks it up
            // with the next accessibility change, or at the latest on the
            // next start.
            manager.addTouchExplorationStateChangeListener(OksaActivity::follow);
        }
        super.onCreate(savedInstanceState);
    }

    // Android's own view of the activity, in Qt's application states:
    // 4 resumed (Active), 2 started (Inactive, on screen), 0 stopped
    // (Suspended). Qt's state can end up Suspended with the app on screen
    // (beta 40: Active and the Suspended of the earlier stop came in that
    // order on return, and the waterfall stayed paused); androidStartup's
    // check sets it right from this.
    private static volatile int lifecycle = 0;

    public static int lifecycleState() {
        return lifecycle;
    }

    @Override
    protected void onStart() {
        super.onStart();
        lifecycle = 2;
    }

    @Override
    protected void onResume() {
        lifecycle = 4;
        super.onResume();
    }

    @Override
    protected void onPause() {
        lifecycle = 2;
        super.onPause();
    }

    @Override
    protected void onStop() {
        lifecycle = 0;
        super.onStop();
    }

    private static void follow(boolean screenReader) {
        try {
            if (screenReader) {
                Os.unsetenv(DISABLE);
            } else {
                Os.setenv(DISABLE, "1", true);
            }
            Log.i(TAG, "accessibility bridge " + (screenReader ? "on (screen reader)" : "off (no screen reader)"));
        } catch (ErrnoException e) {
            Log.w(TAG, "accessibility bridge: " + e);
        }
    }
}
