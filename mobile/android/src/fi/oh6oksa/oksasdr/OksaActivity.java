package fi.oh6oksa.oksasdr;

import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.accessibility.AccessibilityManager;

import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

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
        InputManager input = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (input != null) {
            input.registerInputDeviceListener(dialWatch, null);
        }
    }

    @Override
    protected void onDestroy() {
        InputManager input = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (input != null) {
            input.unregisterInputDeviceListener(dialWatch);
        }
        super.onDestroy();
    }

    // A Ulanzi Dial (D100H, roadmap 83): a Bluetooth keyboard whose knob
    // sends the volume keys, its press Mute, the top keys the media keys
    // and the side keys Ctrl+V / C / Y / Z. While the app has it on (set
    // from C++ once its native ulanziKey is registered), the dial's own keys
    // go there as Linux key codes and nowhere else, so its knob does not
    // change the volume; the phone's own buttons, a headset and other
    // keyboards keep theirs (told apart by the dial's vendor and product).
    private static volatile boolean ulanziOn = false;
    private static final Set<Integer> dialIds = ConcurrentHashMap.newKeySet();
    private static native void ulanziKey(int linuxKey, int value);

    public static void setUlanziOn(boolean on) {
        ulanziOn = on;
    }

    // The dial's name when one is connected, else "".
    public static String ulanziDevice() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (isDial(device)) {
                return device.getName();
            }
        }
        return "";
    }

    private static boolean isDial(InputDevice device) {
        if (device == null) {
            return false;
        }
        if (device.getVendorId() == 0xFFF1 && device.getProductId() == 0x0082) {
            return true;
        }
        String name = device.getName();
        return name != null && name.startsWith("Ulanzi");
    }

    // The Linux code the desktop's decoder takes (KEY_VOLUMEUP 115 ...).
    private static int linuxCode(KeyEvent event) {
        switch (event.getKeyCode()) {
            case KeyEvent.KEYCODE_VOLUME_UP: return 115;
            case KeyEvent.KEYCODE_VOLUME_DOWN: return 114;
            case KeyEvent.KEYCODE_VOLUME_MUTE:
            case KeyEvent.KEYCODE_MUTE: return 113;
            case KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE:
            case KeyEvent.KEYCODE_MEDIA_PLAY:
            case KeyEvent.KEYCODE_MEDIA_PAUSE: return 164;
            case KeyEvent.KEYCODE_MEDIA_PREVIOUS: return 165;
            case KeyEvent.KEYCODE_MEDIA_NEXT: return 163;
            case KeyEvent.KEYCODE_CTRL_LEFT: return 29;
            case KeyEvent.KEYCODE_CTRL_RIGHT: return 97;
            case KeyEvent.KEYCODE_V: return 47;
            case KeyEvent.KEYCODE_C: return 46;
            case KeyEvent.KEYCODE_Y: return 21;
            case KeyEvent.KEYCODE_Z: return 44;
            default: return event.getScanCode();
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (ulanziOn) {
            InputDevice device = event.getDevice();
            if (isDial(device)) {
                dialIds.add(device.getId());
                int action = event.getAction();
                if (action == KeyEvent.ACTION_DOWN || action == KeyEvent.ACTION_UP) {
                    int code = linuxCode(event);
                    if (code > 0) {
                        int value = action == KeyEvent.ACTION_UP ? 0 : event.getRepeatCount() > 0 ? 2 : 1;
                        ulanziKey(code, value);
                    }
                }
                return true;
            }
        }
        return super.dispatchKeyEvent(event);
    }

    // The dial gone (off, out of range): what it held is let go.
    private final InputManager.InputDeviceListener dialWatch = new InputManager.InputDeviceListener() {
        @Override
        public void onInputDeviceAdded(int id) {
        }

        @Override
        public void onInputDeviceChanged(int id) {
        }

        @Override
        public void onInputDeviceRemoved(int id) {
            if (dialIds.remove(id) && ulanziOn) {
                ulanziKey(-1, 0);
            }
        }
    };

    // Android's own view of the activity, in Qt's application states:
    // 4 resumed (Active), 2 started (Inactive, on screen), 0 stopped
    // (Suspended). Qt's state can end up Suspended with the app on screen
    // (beta 40 and 41: Active and then the Suspended of the earlier stop,
    // and the waterfall stayed paused); androidStartup's check sets it right
    // from this. Counted, so that a new instance's start before an old one's
    // stop still reads as on screen.
    private static int started = 0;
    private static int resumed = 0;
    private static volatile int lifecycle = 0;

    public static int lifecycleState() {
        return lifecycle;
    }

    private static void update() {
        lifecycle = resumed > 0 ? 4 : started > 0 ? 2 : 0;
    }

    @Override
    protected void onStart() {
        super.onStart();
        ++started;
        update();
    }

    @Override
    protected void onResume() {
        ++resumed;
        update();
        super.onResume();
    }

    @Override
    protected void onPause() {
        resumed = Math.max(0, resumed - 1);
        update();
        // Away from the screen the dial's keys are the system's, so a held
        // one would never be released here: let go now.
        if (ulanziOn) {
            ulanziKey(-1, 0);
        }
        super.onPause();
    }

    @Override
    protected void onStop() {
        started = Math.max(0, started - 1);
        update();
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
