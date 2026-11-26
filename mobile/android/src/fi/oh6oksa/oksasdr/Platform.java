package fi.oh6oksa.oksasdr;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ComponentName;
import android.content.pm.ResolveInfo;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.net.wifi.WifiManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.ParcelUuid;
import android.os.PowerManager;
import android.provider.Settings;
import android.speech.RecognitionListener;
import android.speech.RecognitionService;
import android.speech.RecognitionSupport;
import android.speech.RecognitionSupportCallback;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;
import androidx.core.content.FileProvider;
import java.io.File;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.UUID;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

// Android services the shared C++ code reaches through JNI: the secure store
// (SmartLink refresh token), the share sheet (log), the multicast lock (LAN
// discovery) and the Bluetooth headset route (voice on a headset).
public final class Platform {
    private Platform() {}

    // ---- Secure store: AES-GCM with a key that never leaves the Android
    // Keystore; only the ciphertext is kept, in app-private preferences.

    private static final String KEY_ALIAS = "oksasdr_secrets";
    private static final String PREFS = "oksasdr_secure";

    private static SecretKey key() throws Exception {
        KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
        ks.load(null);
        if (ks.containsAlias(KEY_ALIAS)) {
            return ((KeyStore.SecretKeyEntry) ks.getEntry(KEY_ALIAS, null)).getSecretKey();
        }
        KeyGenerator gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        gen.init(new KeyGenParameterSpec.Builder(KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .build());
        return gen.generateKey();
    }

    public static boolean saveSecret(Context context, String name, String value) {
        try {
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, key());
            byte[] iv = cipher.getIV();
            byte[] data = cipher.doFinal(value.getBytes(StandardCharsets.UTF_8));
            byte[] blob = new byte[iv.length + data.length];
            System.arraycopy(iv, 0, blob, 0, iv.length);
            System.arraycopy(data, 0, blob, iv.length, data.length);
            // commit(), not apply(): written before this returns, so a sign-in
            // or a sign-out stays even if the app is closed at once.
            return prefs(context).edit().putString(name, Base64.encodeToString(blob, Base64.NO_WRAP)).commit();
        } catch (Exception e) {
            return false;
        }
    }

    public static String loadSecret(Context context, String name) {
        String stored = prefs(context).getString(name, null);
        if (stored == null) {
            return "";
        }
        try {
            byte[] blob = Base64.decode(stored, Base64.NO_WRAP);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(128, blob, 0, 12));
            return new String(cipher.doFinal(blob, 12, blob.length - 12), StandardCharsets.UTF_8);
        } catch (Exception e) {
            return "";
        }
    }

    public static void removeSecret(Context context, String name) {
        prefs(context).edit().remove(name).commit();
    }

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    // ---- Share sheet

    // A spot alert while the app is away from the front (roadmap 132): a
    // notification of its own channel; a tap opens the app. The app asks for
    // notifications (Android 13+) with its background receive.
    private static final String ALERT_CHANNEL = "spot_alerts";
    private static int alertId = 2000;

    public static void postNotification(Context context, String title, String text) {
        NotificationManager nm = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null) {
            return;
        }
        nm.createNotificationChannel(new NotificationChannel(ALERT_CHANNEL, "Spot alerts",
                NotificationManager.IMPORTANCE_HIGH));
        Intent open = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());
        PendingIntent tap = PendingIntent.getActivity(context, 1, open,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification notification = new Notification.Builder(context, ALERT_CHANNEL)
                .setContentTitle(title)
                .setContentText(text)
                .setStyle(new Notification.BigTextStyle().bigText(text))
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .setContentIntent(tap)
                .setAutoCancel(true)
                .build();
        try {
            nm.notify(alertId++, notification);
        } catch (SecurityException e) {
            android.util.Log.w("OksaSDR", "spot alert: no permission to notify: " + e);
        }
    }

    public static boolean share(Context context, String subject, String text) {
        try {
            Intent send = new Intent(Intent.ACTION_SEND);
            send.setType("text/plain");
            send.putExtra(Intent.EXTRA_SUBJECT, subject);
            send.putExtra(Intent.EXTRA_TEXT, text);
            Intent chooser = Intent.createChooser(send, subject);
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(chooser);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    // A file from the app's files folder, through the FileProvider Qt's
    // manifest declares (authority <package>.qtprovider).
    public static boolean shareFile(Context context, String subject, String path, String mimeType) {
        try {
            Uri uri = FileProvider.getUriForFile(context, context.getPackageName() + ".qtprovider", new File(path));
            Intent send = new Intent(Intent.ACTION_SEND);
            send.setType(mimeType);
            send.putExtra(Intent.EXTRA_SUBJECT, subject);
            send.putExtra(Intent.EXTRA_STREAM, uri);
            send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            Intent chooser = Intent.createChooser(send, subject);
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_GRANT_READ_URI_PERMISSION);
            context.startActivity(chooser);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    // A mail to the developer with the log attached: a mail app first (the
    // send restricted to mailto: handlers, started as it is: a chooser
    // around a selector showed nothing on the developer's Android 16), any
    // share target when no mail app takes it. Returns "mail", "share" or
    // "none", with ": " and the reason of what failed, for the app's log.
    public static String sendToDeveloper(Context context, String address, String subject, String body, String path) {
        Uri uri;
        try {
            uri = FileProvider.getUriForFile(context, context.getPackageName() + ".qtprovider", new File(path));
        } catch (Exception e) {
            return "none: no FileProvider URI for the log file: " + e;
        }
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType("text/plain");
        send.putExtra(Intent.EXTRA_EMAIL, new String[] {address});
        send.putExtra(Intent.EXTRA_SUBJECT, subject);
        send.putExtra(Intent.EXTRA_TEXT, body);
        send.putExtra(Intent.EXTRA_STREAM, uri);
        send.setClipData(ClipData.newRawUri(subject, uri));
        send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        String mailFailed;
        try {
            Intent mail = new Intent(send);
            mail.setSelector(new Intent(Intent.ACTION_SENDTO, Uri.parse("mailto:")));
            mail.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(mail);
            return "mail";
        } catch (Exception e) {
            mailFailed = e.toString();
        }
        try {
            Intent chooser = Intent.createChooser(send, subject);
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_GRANT_READ_URI_PERMISSION);
            context.startActivity(chooser);
            return "share: no mail app (" + mailFailed + ")";
        } catch (Exception e) {
            return "none: mail " + mailFailed + "; share sheet " + e;
        }
    }

    // ---- LAN discovery: Wi-Fi drivers may drop broadcast and multicast
    // packets unless an app holds a multicast lock.

    private static WifiManager.MulticastLock multicastLock;

    public static void setMulticast(Context context, boolean on) {
        if (on && multicastLock == null) {
            WifiManager wm = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            multicastLock = wm.createMulticastLock("OksaSDR:discovery");
            multicastLock.setReferenceCounted(false);
            multicastLock.acquire();
        } else if (!on && multicastLock != null) {
            multicastLock.release();
            multicastLock = null;
        }
    }

    // ---- Bluetooth headset: voice (receive and microphone) through a
    // connected headset's hands-free link while a radio is connected, as the
    // iOS app does. Returns the headset's name, or "" when none is in use.

    // The AudioManager the app's own audio route is asked through: one of its
    // own, never the one Qt's audio code holds (roadmap 218). Android keeps
    // an app's requests for a communication device apart by the AudioManager
    // they came through, and Qt 6.12 makes such a request of its own when a
    // microphone opens on a Bluetooth headset and takes it back when that
    // microphone closes (QtAudioDeviceManager.prepareAudioInput and
    // releaseAudioDevice, with clearCommunicationDevice). Through a shared
    // AudioManager that took the app's request with it: the headset's link
    // dropped whenever the microphone closed, after every transmission and
    // whenever the app left the front. Android hands out one AudioManager
    // for each context, so a context of the app's own gives one of its own.
    private static AudioManager ownAudioManager;

    private static synchronized AudioManager audioManager(Context context) {
        if (ownAudioManager == null) {
            Context app = context.getApplicationContext();
            try {
                Context own = app.createConfigurationContext(app.getResources().getConfiguration());
                ownAudioManager = (AudioManager) own.getSystemService(Context.AUDIO_SERVICE);
            } catch (Throwable t) {
                ownAudioManager = null;
            }
            if (ownAudioManager == null) {
                ownAudioManager = (AudioManager) app.getSystemService(Context.AUDIO_SERVICE);
            }
        }
        return ownAudioManager;
    }

    // Whether that AudioManager is another than the one the given context
    // hands out (the one Qt's audio code got): for the log.
    public static boolean audioManagerApart(Context context) {
        try {
            return audioManager(context) != context.getSystemService(Context.AUDIO_SERVICE);
        } catch (Throwable t) {
            return false;
        }
    }

    // The app's own call mode taken again while a headset is the route
    // (roadmap 218): Qt sets Android's audio mode back to normal whenever it
    // opens a microphone, and the app's request for the headset is surest
    // kept while the mode is the app's. Asked just before the microphone
    // held for the link is closed, so that the link stays up through it.
    public static void holdHeadsetMode(Context context) {
        try {
            audioManager(context).setMode(AudioManager.MODE_IN_COMMUNICATION);
        } catch (Throwable t) {
            // Not given: the link's keeper takes the route again if it drops.
        }
    }

    // Tells the app's copy of Qt's audio device class whether the app holds
    // Android's audio mode itself (roadmap 218): while it does, Qt leaves the
    // mode alone when a microphone opens. The mode is one for the whole app,
    // whichever AudioManager sets it, so Qt's "normal" at each opening took
    // the app's call mode away, and holdHeadsetMode put it back when the app
    // left the front: a change of mode at both ends of every visit to the
    // background, and a short break in the sound with each. By name: Qt's
    // class is not public, and a Qt without the app's copy has no such field
    // (nothing happens then, and Qt sets the mode as before).
    private static void holdAudioModeFromQt(boolean hold) {
        try {
            Class<?> qt = Class.forName("org.qtproject.qt.android.multimedia.QtAudioDeviceManager");
            java.lang.reflect.Field field = qt.getDeclaredField("appHoldsAudioMode");
            field.setAccessible(true);
            field.setBoolean(null, hold);
        } catch (Throwable t) {
            // No such field: Qt sets the mode as before.
        }
    }

    public static String useHeadset(Context context, boolean on) {
        AudioManager am = audioManager(context);
        if (!on) {
            if (Build.VERSION.SDK_INT >= 31) {
                am.clearCommunicationDevice();
            } else {
                am.stopBluetoothSco();
                am.setBluetoothScoOn(false);
                am.setSpeakerphoneOn(false);
            }
            holdAudioModeFromQt(false);
            am.setMode(AudioManager.MODE_NORMAL);
            return "";
        }
        watchSco(context);
        if (Build.VERSION.SDK_INT >= 31) {
            List<AudioDeviceInfo> devices = am.getAvailableCommunicationDevices();
            for (AudioDeviceInfo d : devices) {
                int t = d.getType();
                if (t == AudioDeviceInfo.TYPE_BLUETOOTH_SCO || t == AudioDeviceInfo.TYPE_BLE_HEADSET) {
                    am.setMode(AudioManager.MODE_IN_COMMUNICATION);
                    if (am.setCommunicationDevice(d)) {
                        holdAudioModeFromQt(true);
                        return describe(d);
                    }
                    holdAudioModeFromQt(false);
                    am.setMode(AudioManager.MODE_NORMAL);
                }
            }
            return "";
        }
        for (AudioDeviceInfo d : am.getDevices(AudioManager.GET_DEVICES_INPUTS)) {
            if (d.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_SCO) {
                am.setMode(AudioManager.MODE_IN_COMMUNICATION);
                am.startBluetoothSco();
                am.setBluetoothScoOn(true);
                holdAudioModeFromQt(true);
                return describe(d);
            }
        }
        return "";
    }

    // When the headset's hands-free audio link last came up
    // (System.currentTimeMillis()), 0 while it is down; 1 when it was up
    // before the watch started. A receive stream opened while Android
    // switched a headset from its music link to hands-free stayed on the
    // music link, silent in the headset (Bluetooth media -> Auto, beta 41):
    // MobileEngine opens it again when the link came up after it.
    private static volatile long scoUpAt = 0;
    private static boolean scoWatched = false;

    private static synchronized void watchSco(Context context) {
        if (scoWatched) {
            return;
        }
        scoWatched = true;
        BroadcastReceiver receiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context c, Intent intent) {
                int state = intent.getIntExtra(AudioManager.EXTRA_SCO_AUDIO_STATE, AudioManager.SCO_AUDIO_STATE_ERROR);
                if (state != AudioManager.SCO_AUDIO_STATE_CONNECTED) {
                    scoUpAt = 0;
                } else {
                    scoUpAt = isInitialStickyBroadcast() ? 1 : System.currentTimeMillis();
                }
            }
        };
        IntentFilter filter = new IntentFilter(AudioManager.ACTION_SCO_AUDIO_STATE_UPDATED);
        Context app = context.getApplicationContext();
        if (Build.VERSION.SDK_INT >= 33) {
            app.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            app.registerReceiver(receiver, filter);
        }
    }

    public static long scoUpAt() {
        return scoUpAt;
    }

    // Whether receive can reach the headset now: the headset's hands-free
    // audio link is up (roadmap 201: the route asked for is not always the
    // route had; a link that dropped when the microphone closed was taken
    // again only after a transmission). The link's own state, as the watch
    // above has it, not Android's word for the communication device, which
    // lags behind it. An LE Audio headset has no such link: its being the
    // communication device counts.
    public static boolean headsetRouteOk(Context context) {
        if (scoUpAt != 0) {
            return true;
        }
        if (Build.VERSION.SDK_INT >= 31) {
            AudioManager am = audioManager(context);
            AudioDeviceInfo d = am.getCommunicationDevice();
            return d != null && d.getType() == AudioDeviceInfo.TYPE_BLE_HEADSET;
        }
        return false;
    }

    // Whether Android lists a Bluetooth headset as a call device now (roadmap
    // 214): asked when taking the route again came back empty, to tell a
    // headset that is gone (switched off, out of reach) from one Android
    // gave no route this time. The same list useHeadset takes from.
    public static boolean headsetListed(Context context) {
        try {
            AudioManager am = audioManager(context);
            if (Build.VERSION.SDK_INT >= 31) {
                for (AudioDeviceInfo d : am.getAvailableCommunicationDevices()) {
                    int t = d.getType();
                    if (t == AudioDeviceInfo.TYPE_BLUETOOTH_SCO || t == AudioDeviceInfo.TYPE_BLE_HEADSET) {
                        return true;
                    }
                }
                return false;
            }
            for (AudioDeviceInfo d : am.getDevices(AudioManager.GET_DEVICES_INPUTS)) {
                if (d.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_SCO) {
                    return true;
                }
            }
        } catch (Throwable t) {
            // No list: taken as none listed.
        }
        return false;
    }

    // Android's audio mode now (AudioManager.MODE_*): 1 while the phone
    // rings, 2 in a phone call, 4 while a call is screened; 3 is an app's
    // own call mode (ours with a headset), 0 none. During a phone call the
    // headset's route is the call's and is left alone (roadmap 214).
    public static int audioMode(Context context) {
        try {
            return audioManager(context).getMode();
        } catch (Throwable t) {
            return 0;
        }
    }

    // Whether Android's communication route is a Bluetooth headset now.
    public static boolean headsetRoute(Context context) {
        AudioManager am = audioManager(context);
        if (Build.VERSION.SDK_INT >= 31) {
            AudioDeviceInfo d = am.getCommunicationDevice();
            return d != null && (d.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_SCO
                                 || d.getType() == AudioDeviceInfo.TYPE_BLE_HEADSET);
        }
        return am.isBluetoothScoOn();
    }

    // The headset route now, for the log (roadmap 144: receive silent after
    // transmitting with the headset's media audio off): Android's
    // communication device, the hands-free audio link, and whether the
    // headset's media link (A2DP) is there, which Android's Bluetooth
    // settings turn off as the headset's Media audio.
    public static String headsetState(Context context) {
        AudioManager am = audioManager(context);
        String comm;
        if (Build.VERSION.SDK_INT >= 31) {
            AudioDeviceInfo d = am.getCommunicationDevice();
            comm = d == null ? "none" : deviceTypeName(d.getType());
        } else {
            comm = am.isBluetoothScoOn() ? "Bluetooth hands-free" : "not Bluetooth";
        }
        boolean media = false;
        for (AudioDeviceInfo d : am.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
            if (d.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP) {
                media = true;
            }
        }
        long up = scoUpAt;
        return "communication device " + comm + ", hands-free link " + (up == 0 ? "down" : "up")
            + ", media audio " + (media ? "on" : "off") + ", audio mode " + am.getMode();
    }

    private static String deviceTypeName(int type) {
        switch (type) {
            case AudioDeviceInfo.TYPE_BLUETOOTH_SCO: return "Bluetooth hands-free";
            case AudioDeviceInfo.TYPE_BLE_HEADSET: return "Bluetooth LE headset";
            case AudioDeviceInfo.TYPE_BUILTIN_SPEAKER: return "speaker";
            case AudioDeviceInfo.TYPE_BUILTIN_EARPIECE: return "earpiece";
            case AudioDeviceInfo.TYPE_WIRED_HEADSET: return "wired headset";
            case AudioDeviceInfo.TYPE_USB_HEADSET: return "USB headset";
            default: return "type " + type;
        }
    }

    // ---- Device speaker (the operator's choice): receive on the built-in
    // speaker even with Bluetooth audio connected. Android sends media to a
    // connected Bluetooth device on its own, so the speaker is taken as the
    // communication device, as the headset is above. useHeadset(false) ends
    // it. Returns what is in use.

    public static String useSpeaker(Context context) {
        AudioManager am = audioManager(context);
        am.setMode(AudioManager.MODE_IN_COMMUNICATION);
        if (Build.VERSION.SDK_INT >= 31) {
            for (AudioDeviceInfo d : am.getAvailableCommunicationDevices()) {
                if (d.getType() == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER && am.setCommunicationDevice(d)) {
                    return "device speaker";
                }
            }
            am.setMode(AudioManager.MODE_NORMAL);
            return "";
        }
        am.setSpeakerphoneOn(true);
        return "device speaker (speakerphone)";
    }

    // Some phones give a Bluetooth route their own model name as product
    // name, so the link type leads.
    private static String describe(AudioDeviceInfo d) {
        String kind = d.getType() == AudioDeviceInfo.TYPE_BLE_HEADSET ? "LE Audio" : "hands-free";
        return "Bluetooth (" + kind + ", reported as " + d.getProductName() + ")";
    }

    // ---- Keeping the app alive in the background: Android 13+ hides a
    // foreground service's notification without the notification permission,
    // and battery optimization (aggressive on some phones) can still freeze
    // the app. Both asked once, through the system's own dialogs.

    public static String askBackgroundPermissions(Context context) {
        StringBuilder out = new StringBuilder();
        Activity activity = context instanceof Activity ? (Activity) context : null;
        // One request for both, so Android shows its dialogs one after the
        // other: the notification (background receive) and the microphone
        // (PTT; asked here, at the first connect, rather than at the first
        // press, where a press would be lost to the dialog).
        java.util.ArrayList<String> perms = new java.util.ArrayList<>();
        if (Build.VERSION.SDK_INT >= 33) {
            boolean granted = context.checkSelfPermission("android.permission.POST_NOTIFICATIONS")
                    == PackageManager.PERMISSION_GRANTED;
            if (!granted) {
                perms.add("android.permission.POST_NOTIFICATIONS");
            }
            out.append("notifications ").append(granted ? "allowed" : "asked");
        }
        boolean mic = context.checkSelfPermission("android.permission.RECORD_AUDIO")
                == PackageManager.PERMISSION_GRANTED;
        if (!mic) {
            perms.add("android.permission.RECORD_AUDIO");
        }
        out.append(out.length() > 0 ? ", " : "").append("microphone ").append(mic ? "allowed" : "asked");
        if (!perms.isEmpty() && activity != null) {
            activity.requestPermissions(perms.toArray(new String[0]), 4801);
        }
        PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        boolean exempt = pm.isIgnoringBatteryOptimizations(context.getPackageName());
        if (!exempt) {
            try {
                Intent ask = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                        Uri.parse("package:" + context.getPackageName()));
                ask.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                context.startActivity(ask);
            } catch (Exception e) {
                out.append(", battery dialog failed: ").append(e);
            }
        }
        out.append(out.length() > 0 ? ", " : "").append("battery optimization ")
           .append(exempt ? "off for OksaSDR" : "asked");
        return out.toString();
    }

    // ---- Why the previous run ended: reason and description, and for a
    // crash the readable parts of Android's trace (frames, libraries,
    // message), so a crash shows in the next run's log.

    // The Java side of the Android UI thread and Qt's threads, for the stall
    // watch (roadmap 203): a thread that waits in a call up to Android, or
    // on Android's UI thread, shows it here and not in its native frames.
    // Threads with no Java frames (Qt's own, not in a call up) are left out.
    public static String threadStacks() {
        StringBuilder out = new StringBuilder();
        try {
            for (java.util.Map.Entry<Thread, StackTraceElement[]> entry : Thread.getAllStackTraces().entrySet()) {
                String name = entry.getKey().getName();
                StackTraceElement[] frames = entry.getValue();
                if (frames.length == 0 || !(name.equals("main") || name.startsWith("qt") || name.startsWith("Qt")
                        || name.startsWith("QSG") || name.startsWith("QThread"))) {
                    continue;
                }
                out.append(name).append(" (").append(entry.getKey().getState()).append(")\n");
                for (int i = 0; i < frames.length && i < 12; ++i) {
                    out.append("  ").append(frames[i].toString()).append('\n');
                }
            }
        } catch (Throwable t) {
            out.append("none: ").append(t).append('\n');
        }
        return out.toString();
    }

    public static String lastExit(Context context) {
        if (Build.VERSION.SDK_INT < 30) {
            return "";
        }
        try {
            ActivityManager am = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
            List<ApplicationExitInfo> list = am.getHistoricalProcessExitReasons(null, 0, 1);
            if (list.isEmpty()) {
                return "";
            }
            ApplicationExitInfo info = list.get(0);
            StringBuilder out = new StringBuilder();
            out.append("reason=").append(info.getReason())
               .append(" status=").append(info.getStatus())
               .append(" importance=").append(info.getImportance())
               .append(" at=").append(new java.util.Date(info.getTimestamp()))
               .append(" desc=").append(info.getDescription());
            int reason = info.getReason();
            if (reason == ApplicationExitInfo.REASON_CRASH_NATIVE || reason == ApplicationExitInfo.REASON_CRASH
                    || reason == ApplicationExitInfo.REASON_ANR) {
                InputStream in = info.getTraceInputStream();
                if (in != null) {
                    ByteArrayOutputStream buf = new ByteArrayOutputStream();
                    byte[] chunk = new byte[8192];
                    int n;
                    while ((n = in.read(chunk)) > 0 && buf.size() < 512 * 1024) {
                        buf.write(chunk, 0, n);
                    }
                    in.close();
                    out.append("\n").append(readable(buf.toByteArray()));
                }
            }
            return out.toString();
        } catch (Exception e) {
            return "lastExit failed: " + e;
        }
    }

    // Printable runs of the trace (a native crash's trace is a binary
    // tombstone); frames and libraries come out as text.
    private static String readable(byte[] data) {
        StringBuilder out = new StringBuilder();
        StringBuilder run = new StringBuilder();
        int lines = 0;
        for (byte b : data) {
            if (b >= 32 && b < 127) {
                run.append((char) b);
                continue;
            }
            if (run.length() >= 6) {
                String t = run.toString();
                if (t.contains("lib") || t.contains("::") || t.contains("_Z") || t.contains("signal")
                        || t.contains("Abort") || t.contains("Exception") || t.contains("fault")) {
                    out.append("  ").append(t).append("\n");
                    if (++lines >= 80) {
                        break;
                    }
                }
            }
            run.setLength(0);
        }
        return out.toString();
    }

    // ---- Speech to text for logging by voice (roadmap 79): the device's
    // default recogniser listens to the microphone itself while the voice
    // button is held (the app's own capture is closed meanwhile). Beta 85
    // handed it the app's recording instead (Android 13's EXTRA_AUDIO_SOURCE):
    // Google's recogniser over the network gave nothing back for it on the
    // developer's phone, and the on-device one had no English there.
    // Beta 87 on the developer's phone: the recogniser heard speech (its
    // level up to 10 dB) and still answered with an empty list, so the text
    // it gives on the way (partial results) is kept and used when the final
    // answer is empty, and no biasing strings are sent (they are for
    // on-device recognisers). The answer is polled: "ok" and the
    // alternatives a line each, or "error" and the reason; empty while it
    // runs. sSpeechNotes tells the log what the recogniser did: which one,
    // ready, speech heard, its loudest level, what it answered.

    private static volatile String sSpeech = "";
    private static volatile String sSpeechNotes = "";
    private static volatile boolean sSpeechReady = false;
    private static SpeechRecognizer sListening;   // main thread only
    private static volatile boolean sStopWanted = false;
    private static Boolean sOnDeviceEnglish = null;   // main thread only; null: not checked yet

    public static String speechUnavailable(Context context) {
        if (Build.VERSION.SDK_INT < 33) {
            return "Voice input needs Android 13 or later.";
        }
        if (!SpeechRecognizer.isRecognitionAvailable(context)) {
            return "This device has no speech recognition.";
        }
        return "";
    }

    // Called only once speechUnavailable() has passed, which needs Android 13.
    // Which recogniser: the phone's on-device one when it has English
    // installed (nothing leaves the phone; beta 85 asked for its English
    // model), else the Google app's (English over the network), else the
    // default one (on the developer's phone it heard speech and wrote
    // nothing with beta 87, having no English).
    @android.annotation.TargetApi(33)
    public static String startListening(final Context context, final String[] vocabulary) {
        final String why = speechUnavailable(context);
        if (!why.isEmpty()) {
            return why;
        }
        sSpeech = "";
        sSpeechNotes = "";
        sSpeechReady = false;
        sStopWanted = false;
        new Handler(Looper.getMainLooper()).post(() -> {
            if (sListening != null) {
                sListening.destroy();
                sListening = null;
            }
            final Intent intent = speechIntent(context);
            // Beta 90 on the developer's phone: with the alphabet as biasing
            // strings the on-device service disconnected (error 11) at every
            // press, and the probe recogniser destroyed just before a new one
            // did it too (beta 89); without hints its first answers came. So
            // no hints, English is checked once a run, and the probe itself
            // listens.
            if (sOnDeviceEnglish == Boolean.TRUE) {
                listen(context, intent, SpeechRecognizer.createOnDeviceSpeechRecognizer(context), true,
                        "on-device English installed");
                return;
            }
            if (sOnDeviceEnglish == null && SpeechRecognizer.isOnDeviceRecognitionAvailable(context)) {
                try {
                    final SpeechRecognizer probe = SpeechRecognizer.createOnDeviceSpeechRecognizer(context);
                    probe.checkRecognitionSupport(intent, context.getMainExecutor(), new RecognitionSupportCallback() {
                        @Override public void onSupportResult(RecognitionSupport support) {
                            final String english = englishOnDevice(support);
                            sOnDeviceEnglish = english.equals("installed");
                            if (sOnDeviceEnglish) {
                                listen(context, intent, probe, true, "on-device English " + english);
                            } else {
                                probe.destroy();
                                listen(context, intent, null, false, "on-device English " + english);
                            }
                        }
                        @Override public void onError(int error) {
                            probe.destroy();
                            sOnDeviceEnglish = false;
                            listen(context, intent, null, false, "on-device check error " + error);
                        }
                    });
                    return;
                } catch (Exception e) {
                    sOnDeviceEnglish = false;
                }
            }
            listen(context, intent, null, false, sOnDeviceEnglish == null ? "no on-device recogniser" : "the on-device one not used");
        });
        return "";
    }

    @android.annotation.TargetApi(33)
    private static String englishOnDevice(RecognitionSupport support) {
        for (String l : support.getInstalledOnDeviceLanguages()) {
            if (l.toLowerCase(java.util.Locale.ROOT).startsWith("en")) {
                return "installed";
            }
        }
        for (String l : support.getPendingOnDeviceLanguages()) {
            if (l.toLowerCase(java.util.Locale.ROOT).startsWith("en")) {
                return "downloading";
            }
        }
        for (String l : support.getSupportedOnDeviceLanguages()) {
            if (l.toLowerCase(java.util.Locale.ROOT).startsWith("en")) {
                return "not installed";
            }
        }
        return "not offered";
    }

    private static Intent speechIntent(Context context) {
        final Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, "en-US");
        intent.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 5);
        intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true);
        intent.putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, context.getPackageName());
        // A pause between the letters is not the end of the call.
        intent.putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_COMPLETE_SILENCE_LENGTH_MILLIS, 2500);
        intent.putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_POSSIBLY_COMPLETE_SILENCE_LENGTH_MILLIS, 2500);
        return intent;
    }

    @android.annotation.TargetApi(33)
    private static void listen(final Context context, final Intent intent, final SpeechRecognizer given,
                               final boolean onDevice, final String why) {
        try {
            ComponentName google = null;
            StringBuilder found = new StringBuilder();
            for (ResolveInfo info : context.getPackageManager().queryIntentServices(
                    new Intent(RecognitionService.SERVICE_INTERFACE), 0)) {
                if (info.serviceInfo == null) {
                    continue;
                }
                found.append(found.length() == 0 ? "" : " ").append(info.serviceInfo.packageName);
                if (info.serviceInfo.packageName.equals("com.google.android.googlequicksearchbox")) {
                    google = new ComponentName(info.serviceInfo.packageName, info.serviceInfo.name);
                }
            }
            final String fallback = Settings.Secure.getString(context.getContentResolver(), "voice_recognition_service");
            final SpeechRecognizer recognizer = given != null ? given
                    : google != null ? SpeechRecognizer.createSpeechRecognizer(context, google)
                    : SpeechRecognizer.createSpeechRecognizer(context);
            sListening = recognizer;
            sSpeechNotes = why + "; services [" + found + "], default " + (fallback == null ? "none" : fallback)
                    + "; using " + (onDevice ? "the on-device one" : google != null ? google.getPackageName() : "the default")
                    + ": ";
            final float[] loudest = {-100f};
            final ArrayList<String> partial = new ArrayList<>();
            recognizer.setRecognitionListener(new RecognitionListener() {
                @Override public void onReadyForSpeech(Bundle params) {
                    sSpeechReady = true;
                    sSpeechNotes += "ready";
                }
                @Override public void onBeginningOfSpeech() {
                    sSpeechNotes += ", speech began";
                }
                @Override public void onRmsChanged(float rmsdB) {
                    loudest[0] = Math.max(loudest[0], rmsdB);
                }
                @Override public void onBufferReceived(byte[] buffer) {}
                @Override public void onEndOfSpeech() {
                    sSpeechNotes += ", speech ended";
                }
                @Override public void onPartialResults(Bundle partialResults) {
                    ArrayList<String> list = partialResults.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                    if (list != null && !list.isEmpty() && !list.get(0).trim().isEmpty()) {
                        partial.clear();
                        partial.addAll(list);
                    }
                }
                @Override public void onEvent(int eventType, Bundle params) {}
                @Override public void onResults(Bundle results) {
                    ArrayList<String> list = results.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                    final boolean empty = list == null || list.isEmpty() || list.get(0).trim().isEmpty();
                    sSpeechNotes += String.format(java.util.Locale.ROOT, ", loudest %.1f dB, %d result(s)",
                            loudest[0], list == null ? 0 : list.size());
                    if (empty && !partial.isEmpty()) {
                        list = partial;
                        sSpeechNotes += ", the partial result used";
                    }
                    if (list != null && !list.isEmpty()) {
                        sSpeechNotes += ": \"" + list.get(0) + "\"";
                    }
                    sSpeech = "ok\n" + (list == null ? "" : String.join("\n", list));
                    finish(recognizer);
                }
                @Override public void onError(int error) {
                    sSpeechNotes += String.format(java.util.Locale.ROOT, ", loudest %.1f dB, error %d", loudest[0], error);
                    if (onDevice && error == SpeechRecognizer.ERROR_SERVER_DISCONNECTED && !sSpeechReady) {
                        // The on-device service went away before it listened:
                        // the other recogniser takes this press, and the rest
                        // of the run.
                        recognizer.destroy();
                        if (sListening == recognizer) {
                            sListening = null;
                        }
                        sOnDeviceEnglish = false;
                        sSpeechNotes += "; ";
                        listen(context, intent, null, false, "the on-device one disconnected");
                        return;
                    }
                    if (!partial.isEmpty()) {
                        // Heard on the way, then stopped: that is the answer.
                        sSpeechNotes += ", the partial result used: \"" + partial.get(0) + "\"";
                        sSpeech = "ok\n" + String.join("\n", partial);
                    } else {
                        sSpeech = "error\n" + speechError(error);
                    }
                    finish(recognizer);
                }
            });
            recognizer.startListening(intent);
            if (sStopWanted) {
                // Let go while the recogniser was being chosen.
                recognizer.stopListening();
            }
        } catch (Exception e) {
            sSpeech = "error\n" + e;
        }
    }

    // The button let go: the recogniser stops listening and answers.
    public static void stopListening() {
        new Handler(Looper.getMainLooper()).post(() -> {
            sStopWanted = true;
            if (sListening != null) {
                sListening.stopListening();
            }
        });
    }

    private static void finish(SpeechRecognizer recognizer) {
        recognizer.destroy();
        if (sListening == recognizer) {
            sListening = null;
        }
    }

    public static boolean speechReady() {
        return sSpeechReady;
    }

    public static String speechNotes() {
        return sSpeechNotes;
    }

    public static String speechPoll() {
        return sSpeech;
    }

    @android.annotation.TargetApi(33)
    private static String speechError(int error) {
        switch (error) {
            case SpeechRecognizer.ERROR_NO_MATCH: return "Nothing was recognised.";
            case SpeechRecognizer.ERROR_SPEECH_TIMEOUT: return "No speech was heard.";
            case SpeechRecognizer.ERROR_NETWORK:
            case SpeechRecognizer.ERROR_NETWORK_TIMEOUT: return "The recogniser needs the network and has none.";
            case SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS: return "The recogniser has no permission to listen.";
            case SpeechRecognizer.ERROR_RECOGNIZER_BUSY: return "The recogniser is busy.";
            case SpeechRecognizer.ERROR_LANGUAGE_NOT_SUPPORTED:
            case SpeechRecognizer.ERROR_LANGUAGE_UNAVAILABLE: return "English speech recognition is not installed on this device.";
            default: return "Speech recognition failed (" + error + ").";
        }
    }

    // ---- MIDI controllers (roadmap 54): USB ones as Android lists them,
    // Bluetooth LE ones found by a scan (Android has no pairing screen for
    // BLE MIDI) and opened with openBluetoothDevice; the one opened sends
    // its bytes to MidiControlManager through midiReceived. Bluetooth ones
    // opened before are kept by address, so the app opens them again by
    // itself after a restart.

    private static final UUID MIDI_SERVICE = UUID.fromString("03B80E5A-EDE8-4B33-A751-6CE34EC4C700");
    private static final String MIDI_PREFS = "oksasdr_midi_ble";
    private static final int MIDI_SCAN_MS = 15000;
    private static MidiManager sMidi;
    private static final LinkedHashMap<String, String> sBleMidi = new LinkedHashMap<>();   // name -> address
    private static MidiDevice sMidiDevice;
    private static MidiOutputPort sMidiPort;
    private static MidiDeviceInfo sMidiInfo;
    private static volatile long sMidiTarget;
    // 0 closed, 1 opening, 2 open.
    private static volatile int sMidiState;
    private static volatile boolean sMidiScanning;
    private static String sMidiScanError = "";
    private static ScanCallback sScanCallback;

    private static native void midiReceived(long target, byte[] data, int offset, int count);

    private static MidiManager midiManager(Context context) {
        if (sMidi == null) {
            sMidi = (MidiManager) context.getSystemService(Context.MIDI_SERVICE);
            if (sMidi != null) {
                // A controller unplugged or switched off: closed, so what it keyed ends.
                sMidi.registerDeviceCallback(new MidiManager.DeviceCallback() {
                    @Override
                    public void onDeviceRemoved(MidiDeviceInfo info) {
                        MidiDeviceInfo open = sMidiInfo;
                        if (open != null && open.getId() == info.getId()) {
                            closeMidi();
                        }
                    }
                }, new Handler(Looper.getMainLooper()));
            }
            synchronized (sBleMidi) {
                SharedPreferences prefs = context.getSharedPreferences(MIDI_PREFS, Context.MODE_PRIVATE);
                for (java.util.Map.Entry<String, ?> e : prefs.getAll().entrySet()) {
                    sBleMidi.put(e.getKey(), String.valueOf(e.getValue()));
                }
            }
        }
        return sMidi;
    }

    private static String midiName(MidiDeviceInfo info) {
        Bundle p = info.getProperties();
        String name = p.getString(MidiDeviceInfo.PROPERTY_NAME);
        if (name == null || name.isEmpty()) {
            name = p.getString(MidiDeviceInfo.PROPERTY_PRODUCT);
        }
        return name == null || name.isEmpty() ? "MIDI " + info.getId() : name;
    }

    // The controllers the app can listen to: devices that send (an output
    // port), then the Bluetooth ones found or opened before.
    public static String[] midiDevices(Context context) {
        LinkedHashSet<String> names = new LinkedHashSet<>();
        MidiManager m = midiManager(context);
        if (m != null) {
            for (MidiDeviceInfo info : m.getDevices()) {
                if (info.getOutputPortCount() > 0) {
                    names.add(midiName(info));
                }
            }
        }
        synchronized (sBleMidi) {
            names.addAll(sBleMidi.keySet());
        }
        return names.toArray(new String[0]);
    }

    public static boolean openMidi(Context context, String name, long target) {
        closeMidi();
        MidiManager m = midiManager(context);
        if (m == null) {
            return false;
        }
        Handler main = new Handler(Looper.getMainLooper());
        for (MidiDeviceInfo info : m.getDevices()) {
            if (info.getOutputPortCount() > 0 && midiName(info).equals(name)) {
                sMidiState = 1;
                sMidiTarget = target;
                m.openDevice(info, device -> midiOpened(context, device, name, target), main);
                return true;
            }
        }
        String address;
        synchronized (sBleMidi) {
            address = sBleMidi.get(name);
        }
        BluetoothAdapter adapter = bluetoothAdapter(context);
        if (address == null || adapter == null || !adapter.isEnabled() || !mayConnectBluetooth(context)) {
            return false;
        }
        sMidiState = 1;
        sMidiTarget = target;
        m.openBluetoothDevice(adapter.getRemoteDevice(address), device -> midiOpened(context, device, name, target), main);
        return true;
    }

    private static void midiOpened(Context context, MidiDevice device, String name, long target) {
        if (device == null || sMidiTarget != target || sMidiState != 1) {
            if (device != null) {
                try { device.close(); } catch (IOException e) { }
            } else if (sMidiTarget == target) {
                sMidiState = 0;
            }
            return;
        }
        MidiDeviceInfo info = device.getInfo();
        int portIndex = 0;
        for (MidiDeviceInfo.PortInfo port : info.getPorts()) {
            if (port.getType() == MidiDeviceInfo.PortInfo.TYPE_OUTPUT) {
                portIndex = port.getPortNumber();
                break;
            }
        }
        MidiOutputPort port = device.openOutputPort(portIndex);
        if (port == null) {
            try { device.close(); } catch (IOException e) { }
            sMidiState = 0;
            return;
        }
        port.connect(new MidiReceiver() {
            @Override
            public void onSend(byte[] msg, int offset, int count, long timestamp) {
                long to = sMidiTarget;
                if (to != 0 && sMidiPort == port) {
                    midiReceived(to, msg, offset, count);
                }
            }
        });
        sMidiDevice = device;
        sMidiPort = port;
        sMidiInfo = info;
        sMidiState = 2;
        if (info.getType() == MidiDeviceInfo.TYPE_BLUETOOTH) {
            BluetoothDevice bt = info.getProperties().getParcelable(MidiDeviceInfo.PROPERTY_BLUETOOTH_DEVICE);
            if (bt != null) {
                synchronized (sBleMidi) {
                    sBleMidi.put(name, bt.getAddress());
                }
                context.getSharedPreferences(MIDI_PREFS, Context.MODE_PRIVATE).edit()
                        .putString(name, bt.getAddress()).commit();
            }
        }
    }

    public static void closeMidi() {
        sMidiTarget = 0;
        sMidiState = 0;
        MidiOutputPort port = sMidiPort;
        MidiDevice device = sMidiDevice;
        sMidiPort = null;
        sMidiDevice = null;
        sMidiInfo = null;
        try {
            if (port != null) port.close();
        } catch (IOException e) { }
        try {
            if (device != null) device.close();
        } catch (IOException e) { }
    }

    public static boolean midiOpen() {
        return sMidiState != 0;
    }

    private static BluetoothAdapter bluetoothAdapter(Context context) {
        BluetoothManager bm = (BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE);
        return bm == null ? null : bm.getAdapter();
    }

    private static boolean mayConnectBluetooth(Context context) {
        return Build.VERSION.SDK_INT < 31
                || context.checkSelfPermission("android.permission.BLUETOOTH_CONNECT") == PackageManager.PERMISSION_GRANTED;
    }

    // A 15 s scan for Bluetooth LE MIDI controllers (the MIDI service UUID);
    // what it finds joins midiDevices. Empty when it started, else why not
    // (the permission is asked for then: tap again once it is given).
    public static String scanBluetoothMidi(Context context) {
        Activity activity = context instanceof Activity ? (Activity) context : null;
        java.util.ArrayList<String> missing = new java.util.ArrayList<>();
        String[] needed = Build.VERSION.SDK_INT >= 31
                ? new String[] {"android.permission.BLUETOOTH_SCAN", "android.permission.BLUETOOTH_CONNECT"}
                : new String[] {"android.permission.ACCESS_FINE_LOCATION"};
        for (String p : needed) {
            if (context.checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) {
                missing.add(p);
            }
        }
        if (!missing.isEmpty()) {
            if (activity != null) {
                activity.requestPermissions(missing.toArray(new String[0]), 4802);
            }
            return Build.VERSION.SDK_INT >= 31
                    ? "Allow Nearby devices for OksaSDR, then tap Pair again."
                    : "Allow Location for OksaSDR (Android needs it to find Bluetooth devices), then tap Pair again.";
        }
        BluetoothAdapter adapter = bluetoothAdapter(context);
        if (adapter == null) {
            return "This device has no Bluetooth.";
        }
        if (!adapter.isEnabled()) {
            return "Turn Bluetooth on, then tap Pair again.";
        }
        BluetoothLeScanner scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) {
            return "Bluetooth is not ready yet: tap Pair again.";
        }
        midiManager(context);
        if (sScanCallback != null) {
            try { scanner.stopScan(sScanCallback); } catch (RuntimeException e) { }
        }
        sScanCallback = new ScanCallback() {
            @Override
            public void onScanResult(int callbackType, ScanResult result) {
                BluetoothDevice device = result.getDevice();
                ScanRecord record = result.getScanRecord();
                String name = record != null ? record.getDeviceName() : null;
                if (name == null || name.isEmpty()) {
                    try { name = device.getName(); } catch (SecurityException e) { name = null; }
                }
                if (name == null || name.isEmpty()) {
                    name = "Bluetooth MIDI " + device.getAddress();
                }
                synchronized (sBleMidi) {
                    sBleMidi.put(name, device.getAddress());
                }
            }

            @Override
            public void onScanFailed(int errorCode) {
                sMidiScanError = "The Bluetooth search failed (" + errorCode + ").";
                sMidiScanning = false;
            }
        };
        List<ScanFilter> filters = new ArrayList<>();
        filters.add(new ScanFilter.Builder().setServiceUuid(new ParcelUuid(MIDI_SERVICE)).build());
        ScanSettings settings = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
        final ScanCallback callback = sScanCallback;
        try {
            scanner.startScan(filters, settings, callback);
        } catch (SecurityException e) {
            return "Allow Nearby devices for OksaSDR, then tap Pair again.";
        }
        sMidiScanError = "";
        sMidiScanning = true;
        new Handler(Looper.getMainLooper()).postDelayed(() -> {
            try { scanner.stopScan(callback); } catch (RuntimeException e) { }
            if (sScanCallback == callback) {
                sMidiScanning = false;
            }
        }, MIDI_SCAN_MS);
        return "";
    }

    public static boolean midiScanning() {
        return sMidiScanning;
    }

    public static String midiScanError() {
        return sMidiScanError;
    }
}
