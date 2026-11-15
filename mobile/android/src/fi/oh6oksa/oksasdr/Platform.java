package fi.oh6oksa.oksasdr;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
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
import android.net.wifi.WifiManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
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
import java.util.List;

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

    public static String useHeadset(Context context, boolean on) {
        AudioManager am = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        if (!on) {
            if (Build.VERSION.SDK_INT >= 31) {
                am.clearCommunicationDevice();
            } else {
                am.stopBluetoothSco();
                am.setBluetoothScoOn(false);
                am.setSpeakerphoneOn(false);
            }
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
                        return describe(d);
                    }
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

    // Whether Android's communication route is a Bluetooth headset now.
    public static boolean headsetRoute(Context context) {
        AudioManager am = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        if (Build.VERSION.SDK_INT >= 31) {
            AudioDeviceInfo d = am.getCommunicationDevice();
            return d != null && (d.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_SCO
                                 || d.getType() == AudioDeviceInfo.TYPE_BLE_HEADSET);
        }
        return am.isBluetoothScoOn();
    }

    // ---- Device speaker (the operator's choice): receive on the built-in
    // speaker even with Bluetooth audio connected. Android sends media to a
    // connected Bluetooth device on its own, so the speaker is taken as the
    // communication device, as the headset is above. useHeadset(false) ends
    // it. Returns what is in use.

    public static String useSpeaker(Context context) {
        AudioManager am = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
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
}
