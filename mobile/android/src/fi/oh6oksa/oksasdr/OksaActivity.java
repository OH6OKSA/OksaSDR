package fi.oh6oksa.oksasdr;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.accessibility.AccessibilityManager;

import java.util.Locale;
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
        IntentFilter ptt = new IntentFilter();
        for (String action : PTT_ACTIONS) {
            ptt.addAction(action);
        }
        try {
            if (Build.VERSION.SDK_INT >= 33) {
                registerReceiver(pttWatch, ptt, Context.RECEIVER_EXPORTED);
            } else {
                registerReceiver(pttWatch, ptt);
            }
            pttState = "listening for " + PTT_ACTIONS.length + " push-to-talk broadcasts";
        } catch (RuntimeException e) {
            pttState = "push-to-talk broadcasts not listened for: " + e;
        }
    }

    @Override
    protected void onDestroy() {
        InputManager input = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (input != null) {
            input.unregisterInputDeviceListener(dialWatch);
        }
        try {
            unregisterReceiver(pttWatch);
        } catch (IllegalArgumentException e) {
            // never registered
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
        logKey(event);
        return super.dispatchKeyEvent(event);
    }

    // The key log (roadmap 119, Rob Metallo VA3MET's UNIWA F400, whose side
    // PTT key works with Zello and other push-to-talk apps): a handset's PTT
    // key reaches apps as a key (often a vendor code, or one Android cannot
    // name, with its scan code) or as a broadcast push-to-talk apps listen
    // for. Both are written to the app's log, so one press and Log > Send to
    // the developer tell which; nothing is keyed by them in this step. Keys
    // from a full keyboard are the app's own (Space PTT, the arrows, the
    // F-keys), so from one only a key Android cannot name is logged; any other
    // device's (the handset's buttons, a remote, a Bluetooth PTT button) are.
    private static volatile boolean keyLogOn = false;
    private static native void keyLog(String line);
    private static final int KEY_LINES_MAX = 40;
    private static int keyLines = 0;
    private static String pttState = "push-to-talk broadcasts not listened for";

    // Zello's, which handset makers send for it (support.zello.com, hardware
    // partner integration), and the handsets' own in public documents:
    // RugGear's and other MediaTek handsets', Sonim's, Zebra's.
    private static final String[] PTT_ACTIONS = {
        "com.zello.ptt.down", "com.zello.ptt.up",
        "com.zello.intent.buttonExtra1.down", "com.zello.intent.buttonExtra1.up",
        "android.intent.action.PTT.down", "android.intent.action.PTT.up",
        "com.sonim.intent.action.PTT_KEY_DOWN", "com.sonim.intent.action.PTT_KEY_UP",
        "com.symbol.button.L2",
    };

    // From C++ once its native keyLog is registered (android/KeyLog.cpp).
    public static void setKeyLogOn(boolean on) {
        keyLogOn = on;
        if (on) {
            note("key log: on (keys from devices other than a full keyboard, and keys Android cannot name); " + pttState);
        }
    }

    private static void note(String line) {
        if (keyLogOn) {
            try {
                keyLog(line);
                return;
            } catch (UnsatisfiedLinkError e) {
                keyLogOn = false;
            }
        }
        Log.i(TAG, line);
    }

    // The first lines of a run only: a held or chattering key fills no log.
    private static boolean takeLine() {
        if (keyLines < KEY_LINES_MAX) {
            ++keyLines;
            return true;
        }
        if (keyLines == KEY_LINES_MAX) {
            ++keyLines;
            note("key log: " + KEY_LINES_MAX + " lines, no more in this run");
        }
        return false;
    }

    private static String describe(InputDevice device) {
        if (device == null) {
            return "no device";
        }
        String where = Build.VERSION.SDK_INT >= 29 ? (device.isExternal() ? " external" : " built-in") : "";
        return String.format(Locale.ROOT, "\"%s\" %04x:%04x%s", device.getName(), device.getVendorId(),
                             device.getProductId(), where);
    }

    private static void logKey(KeyEvent event) {
        int action = event.getAction();
        if ((action != KeyEvent.ACTION_DOWN && action != KeyEvent.ACTION_UP) || event.getRepeatCount() > 0) {
            return;
        }
        InputDevice device = event.getDevice();
        int code = event.getKeyCode();
        boolean keyboard = device != null && device.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC;
        if (keyboard && code != KeyEvent.KEYCODE_UNKNOWN) {
            return;
        }
        if (!takeLine()) {
            return;
        }
        note(String.format(Locale.ROOT, "key %s: %s (%d) scan %d from %s",
                           action == KeyEvent.ACTION_DOWN ? "down" : "up", KeyEvent.keyCodeToString(code), code,
                           event.getScanCode(), describe(device)));
    }

    private final BroadcastReceiver pttWatch = new BroadcastReceiver() {
        // Bundle.get: the extras' types are the sender's, unknown here.
        @SuppressWarnings("deprecation")
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!takeLine()) {
                return;
            }
            StringBuilder line = new StringBuilder("ptt broadcast: ").append(intent.getAction());
            Bundle extras = intent.getExtras();
            if (extras != null) {
                for (String key : extras.keySet()) {
                    line.append(' ').append(key).append('=').append(String.valueOf(extras.get(key)));
                    if (line.length() > 300) {
                        line.append(" ...");
                        break;
                    }
                }
            }
            note(line.toString());
        }
    };

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
