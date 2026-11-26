// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

// OksaSDR: Qt 6.12.0's QtAudioDeviceManager.java (qtmultimedia
// src/android/jar) with USB headsets (AudioDeviceInfo.TYPE_USB_HEADSET,
// what USB-C headsets and USB-C to 3.5 mm adapters report) known: Qt's
// device list left them out as an unknown type, so Qt opened receive and
// the microphone on the phone's own speaker and microphone with a USB-C
// headset plugged in (Jeff Stinger, Pixel 7). They rank with wired
// headsets. mobile/android/patch-qt-usb-audio.sh compiles this into the Qt
// install's Qt6AndroidMultimedia.jar; the changes are marked OksaSDR.
//
// Also (roadmap 219): a Bluetooth headset's two devices both listed. Qt kept
// only the first Bluetooth device Android names, the call device (SCO) or the
// music device (A2DP) as it happened, and called both "Bluetooth": on a phone
// that names the call device first the app could not open receive on the
// music device at all, so Audio output > Bluetooth media stayed on the
// phone's speaker or played on a call link that was not up. Now one of each
// is listed, the call device first (it stays Qt's default output with a
// headset, which Audio output > Auto uses) and the music device after it,
// named "Bluetooth media".
//
// And (roadmap 218): Android's audio mode left alone while the app holds it.
// With a Bluetooth headset as its route the app sets its own call mode, which
// keeps the headset's link up with no microphone open (in the background).
// Qt set the mode back to normal at every opening of a microphone, so the
// mode changed each time the app left the front and came back, and the sound
// broke off for a moment at each change. appHoldsAudioMode is set by the
// app's Platform class when it takes the headset's route and cleared when it
// lets it go.

package org.qtproject.qt.android.multimedia;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;
import android.content.Context;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;

import org.qtproject.qt.android.UsedFromNativeCode;

class QtAudioDeviceManager
{
    private static final String TAG = "QtAudioDeviceManager";
    // OksaSDR: the mark patch-qt-usb-audio.sh looks for in the compiled class.
    private static final String OKSASDR_PATCH = "OksaSDR audio patch 3";
    // OksaSDR: the app holds Android's audio mode itself (roadmap 218).
    static volatile boolean appHoldsAudioMode = false;

    static private AudioManager m_audioManager = null;
    static private final AudioDevicesReceiver m_audioDevicesReceiver = new AudioDevicesReceiver();
    static private HandlerThread m_handlerThread = null;
    static private Handler m_handler = null;
    static private AudioRecord m_recorder = null;
    static private AudioTrack m_streamPlayer = null;
    static private Thread m_streamingThread = null;
    static private boolean m_isStreaming = false;
    static private boolean m_useSpeaker = false;
    static private final int m_sampleRate = 8000;
    static private final int m_channels = AudioFormat.CHANNEL_CONFIGURATION_MONO;
    static private final int m_audioFormat = AudioFormat.ENCODING_PCM_16BIT;
    static private final int m_bufferSize = AudioRecord.getMinBufferSize(m_sampleRate, m_channels, m_audioFormat);
    static private int m_currentOutputId = -1;
    static private AtomicInteger m_scoCounter = new AtomicInteger();
    static private AtomicInteger m_communicationDeviceCounter = new AtomicInteger();
    static private AtomicInteger m_speakerphoneCounter = new AtomicInteger();
    static private int m_currentCommunicationDeviceId = -1;

    // Holds the pointer to the QAndroidAudioDevices instance.
    //
    // This is always assigned with callbacks registered, from a
    // C++ thread. Assignment only happens when QAndroidAudioDevices
    // is constructed. It is always cleared from the Handler thread, using
    // blocking job from the C++ thread.
    static private long m_qAudioDevicesNativePtr = 0;

    static native void onAudioInputDevicesUpdated(long qAudioDeviceNativePtr);
    static native void onAudioOutputDevicesUpdated(long qAudioDeviceNativePtr);

    static private void updateDeviceList() {
        assert(m_handler.getLooper().isCurrentThread());
        // Make sure we never get a callback when we are unregistered.
        assert(m_qAudioDevicesNativePtr != 0);

        if (m_currentOutputId != -1)
            prepareAudioOutput(m_currentOutputId);
        onAudioInputDevicesUpdated(m_qAudioDevicesNativePtr);
        onAudioOutputDevicesUpdated(m_qAudioDevicesNativePtr);
    }

    private static class AudioDevicesReceiver extends AudioDeviceCallback {
        @Override
        public void onAudioDevicesAdded(AudioDeviceInfo[] addedDevices) {
            assert(m_handler.getLooper().isCurrentThread());
            updateDeviceList();
        }

        @Override
        public void onAudioDevicesRemoved(AudioDeviceInfo[] removedDevices) {
            assert(m_handler.getLooper().isCurrentThread());
            updateDeviceList();
        }
    }

    @UsedFromNativeCode
    static void qAndroidAudioDevicesConstructed(long nativePtr)
    {
        assert(nativePtr != 0);
        assert(m_handlerThread == null);
        m_qAudioDevicesNativePtr = nativePtr;
        m_handlerThread = new HandlerThread("QtAudioDeviceCallbacks");
        m_handlerThread.start();
        m_handler = new Handler(m_handlerThread.getLooper());
        m_audioManager.registerAudioDeviceCallback(m_audioDevicesReceiver, m_handler);
    }

    @UsedFromNativeCode
    static void qAndroidAudioDevicesDestroyed()
    {
        assert(!m_handler.getLooper().isCurrentThread());

        // Perform cleanup on the same thread that we receive callbacks, then wait for cleanup job
        // to finish. No need for locks.
        final CountDownLatch latch = new CountDownLatch(1);
        final boolean postSuccess = m_handler.post(() -> {
            m_audioManager.unregisterAudioDeviceCallback(m_audioDevicesReceiver);
            assert(m_qAudioDevicesNativePtr != 0);
            m_qAudioDevicesNativePtr = 0;
            latch.countDown(); // Signal that the task is done
        });

        if (!postSuccess) {
            Log.w(
                TAG,
                "Unable to post cleanup job to corresponding thread during " +
                "QAndroidAudioDevices cleanup.");
            return;
        }

        try {
            latch.await();
        } catch (Exception e) {
            Log.w(
                TAG,
                "Unable to wait for cleanup job to finish on corresponding thread during" +
                "QAndroidAudioDevices cleanup.");
        }

        m_handlerThread.quitSafely();
        m_handlerThread = null;
        m_handler = null;
    }

    static void setContext(Context context)
    {
        Log.i(TAG, OKSASDR_PATCH); // OksaSDR
        m_audioManager = (AudioManager)context.getSystemService(Context.AUDIO_SERVICE);
    }

    static AudioDeviceInfo[] getAudioOutputDevices()
    {
        return getAudioDevices(AudioManager.GET_DEVICES_OUTPUTS);
    }

    static AudioDeviceInfo[] getAudioInputDevices()
    {
        return getAudioDevices(AudioManager.GET_DEVICES_INPUTS);
    }

    @UsedFromNativeCode
    static AudioDeviceInfo getAudioInputDeviceInfo(int id)
    {
        final AudioDeviceInfo[] audioDevices = m_audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS);

        for (AudioDeviceInfo deviceInfo : audioDevices) {
            if (deviceInfo.getId() == id)
                return deviceInfo;
        }

        return null;
    }

    @UsedFromNativeCode
    static int getDefaultSampleRate()
    {
        String sampleRate = m_audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE);
        return Integer.parseInt(sampleRate);
    }

    @UsedFromNativeCode
    static boolean isBluetoothDevice(AudioDeviceInfo deviceInfo)
    {
        switch (deviceInfo.getType()) {
        case AudioDeviceInfo.TYPE_BLUETOOTH_A2DP:
        case AudioDeviceInfo.TYPE_BLUETOOTH_SCO:
            return true;
        default:
            return false;
        }
    }

    @UsedFromNativeCode
    static boolean prepareAudioInput(int id)
    {
        final AudioDeviceInfo[] audioDevices =
                                        m_audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS);
        for (AudioDeviceInfo deviceInfo : audioDevices) {
            if (deviceInfo.getId() == id) {
               switch (deviceInfo.getType())
               {
                   case AudioDeviceInfo.TYPE_BLUETOOTH_A2DP:
                   case AudioDeviceInfo.TYPE_BLUETOOTH_SCO:
                   case AudioDeviceInfo.TYPE_WIRED_HEADSET:
                   case AudioDeviceInfo.TYPE_USB_HEADSET:
                   case AudioDeviceInfo.TYPE_BUILTIN_MIC:
                       return prepareAudioDevice(deviceInfo, AudioManager.MODE_NORMAL);
                   default:
                       return true;
               }
           }
        }
        return false;
    }

    private static void setInputMuted(boolean mute)
    {
        // This method mutes the microphone across the entire platform
        m_audioManager.setMicrophoneMute(mute);
    }

    private static boolean isMicrophoneMute()
    {
        return m_audioManager.isMicrophoneMute();
    }

    private static String audioDeviceTypeToString(int type)
    {
        // API <= 23 types
        switch (type)
        {
            case AudioDeviceInfo.TYPE_AUX_LINE:
                return "AUX Line";
            case AudioDeviceInfo.TYPE_BLUETOOTH_A2DP:
                return "Bluetooth media"; // OksaSDR: the music device, told apart
            case AudioDeviceInfo.TYPE_BLUETOOTH_SCO:
                return "Bluetooth";
            case AudioDeviceInfo.TYPE_BUILTIN_EARPIECE:
                return "Built in earpiece";
            case AudioDeviceInfo.TYPE_BUILTIN_MIC:
                return "Built in microphone";
            case AudioDeviceInfo.TYPE_BUILTIN_SPEAKER:
                return "Built in speaker";
            case AudioDeviceInfo.TYPE_DOCK:
                return "Dock";
            case AudioDeviceInfo.TYPE_FM:
                return "FM";
            case AudioDeviceInfo.TYPE_FM_TUNER:
                return "FM TUNER";
            case AudioDeviceInfo.TYPE_HDMI:
                return "HDMI";
            case AudioDeviceInfo.TYPE_HDMI_ARC:
                return "HDMI ARC";
            case AudioDeviceInfo.TYPE_IP:
                return "IP";
            case AudioDeviceInfo.TYPE_LINE_ANALOG:
                return "Line analog";
            case AudioDeviceInfo.TYPE_LINE_DIGITAL:
                return "Line digital";
            case AudioDeviceInfo.TYPE_TV_TUNER:
                return "TV tuner";
            case AudioDeviceInfo.TYPE_USB_ACCESSORY:
                return "USB accessory";
            case AudioDeviceInfo.TYPE_USB_DEVICE:
                return "USB device";
            case AudioDeviceInfo.TYPE_WIRED_HEADPHONES:
                return "Wired headphones";
            case AudioDeviceInfo.TYPE_WIRED_HEADSET:
                return "Wired headset";
            case AudioDeviceInfo.TYPE_USB_HEADSET: // OksaSDR
                return "USB headset";
        }

        // API 24
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            if (type == AudioDeviceInfo.TYPE_BUS)
                return "Bus";
        }

        return "Unknown-Type";

    }

    private static boolean isDeviceTypeSupported(int type)
    {
        return audioDeviceTypeToString(type)
                   != audioDeviceTypeToString(AudioDeviceInfo.TYPE_UNKNOWN);
    }

    private static final HashMap<Integer, Integer> priorityMap = new HashMap<Integer, Integer>() {{
        put(AudioDeviceInfo.TYPE_WIRED_HEADSET, 1);
        put(AudioDeviceInfo.TYPE_WIRED_HEADPHONES, 1);
        put(AudioDeviceInfo.TYPE_USB_DEVICE, 1);
        put(AudioDeviceInfo.TYPE_USB_HEADSET, 1); // OksaSDR
        // NOTE: Should TYPE_USB_ACCESSORY also have priority level 1?
        // OksaSDR: the call device before the music device, both before the
        // speaker (they shared level 2, so Android's order decided).
        put(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 2);
        put(AudioDeviceInfo.TYPE_BLUETOOTH_A2DP, 3);
        put(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER, 4);
    }};
    private static final int DEFAULT_PRIORITY = 5;

    private static void sortAudioDevices(AudioDeviceInfo[] devices) {
        Comparator<AudioDeviceInfo> deviceTypeComparator = new Comparator<AudioDeviceInfo>() {
            @Override
            public int compare(AudioDeviceInfo device1, AudioDeviceInfo device2) {
                return getPriority(device1) - getPriority(device2);
            }

            private int getPriority(AudioDeviceInfo device) {
                return priorityMap.getOrDefault(device.getType(), DEFAULT_PRIORITY);
            }
        };

       Arrays.sort(devices, deviceTypeComparator);
    }

    private static AudioDeviceInfo[] getAudioDevices(int type)
    {
        ArrayList<AudioDeviceInfo> filteredDevices = new ArrayList<>();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            boolean builtInMicAdded = false;
            boolean bluetoothScoAdded = false;  // OksaSDR: one of each kind
            boolean bluetoothA2dpAdded = false; // OksaSDR
            AudioDeviceInfo[] audioDevices = m_audioManager.getDevices(type);
            sortAudioDevices(audioDevices);

            for (AudioDeviceInfo deviceInfo : audioDevices) {
                if (!isDeviceTypeSupported(deviceInfo.getType()))
                    continue;

                String deviceType = audioDeviceTypeToString(deviceInfo.getType());
                if (deviceType.equals(audioDeviceTypeToString(AudioDeviceInfo.TYPE_BUILTIN_MIC))) {
                    if (builtInMicAdded) {
                        // Built in mic already added. Second built in mic is CAMCORDER, but there
                        // is no reliable way of selecting it. AudioSource.MIC usually means the
                        // primary mic. AudioSource.CAMCORDER source might mean the secondary mic,
                        // but there's no guarantee. It depends e.g. on the physical placement
                        // of the mics. That's why we will not add built in microphone twice.
                        // Should we?
                        continue;
                    }
                    builtInMicAdded = true;
                } else if (isBluetoothDevice(deviceInfo)) {
                    // OksaSDR: a headset's call device (SCO) and its music
                    // device (A2DP) are two outputs with two uses, so one of
                    // each kind is listed; Qt listed the first Bluetooth
                    // device only, whichever kind Android named first.
                    if (deviceInfo.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP) {
                        if (bluetoothA2dpAdded)
                            continue;
                        bluetoothA2dpAdded = true;
                    } else {
                        if (bluetoothScoAdded)
                            continue;
                        bluetoothScoAdded = true;
                    }
                }

                filteredDevices.add(deviceInfo);
            }
        }

        return filteredDevices.toArray(new AudioDeviceInfo[filteredDevices.size()]);
    }

    final private static int [] bluetoothTypes = {
            AudioDeviceInfo.TYPE_BLUETOOTH_A2DP,
            AudioDeviceInfo.TYPE_BLUETOOTH_SCO
    };
    final private static int [] wiredTypes = {
            AudioDeviceInfo.TYPE_WIRED_HEADSET,
            AudioDeviceInfo.TYPE_WIRED_HEADPHONES,
            AudioDeviceInfo.TYPE_USB_HEADSET // OksaSDR
    };

    private static boolean containsAnyOfType(AudioDeviceInfo[] devices, int[]... types) {

    return Arrays.stream(devices)
                 .anyMatch(device -> Arrays.stream(types)
                                           .flatMapToInt(Arrays::stream)
                                           .anyMatch(type -> device.getType() == type));
    }

    private static int getCorrectModeIfContainsAnyOfType(AudioDeviceInfo[] devices, int[]... types) {
        return containsAnyOfType(devices, types) ?
                AudioManager.MODE_IN_COMMUNICATION : AudioManager.MODE_NORMAL;
    }

    private static int getModeForWired(AudioDeviceInfo[] audioDevices)
    {
        return getCorrectModeIfContainsAnyOfType(audioDevices, bluetoothTypes);
    }

    private static int getModeForBluetooth(AudioDeviceInfo[] audioDevices)
    {
        return getCorrectModeIfContainsAnyOfType(audioDevices, wiredTypes);
    }

    private static int getModeForSpeaker(AudioDeviceInfo[] audioDevices)
    {
        return getCorrectModeIfContainsAnyOfType(audioDevices, bluetoothTypes, wiredTypes);
    }

    private static boolean prepareAudioOutput(int id)
    {
        final AudioDeviceInfo[] audioDevices =
                                        m_audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);
        for (AudioDeviceInfo deviceInfo : audioDevices) {
           if (deviceInfo.getId() == id) {
               switch (deviceInfo.getType())
               {
                   case AudioDeviceInfo.TYPE_BLUETOOTH_A2DP:
                   case AudioDeviceInfo.TYPE_BLUETOOTH_SCO:
                       return prepareAudioDevice(deviceInfo, getModeForBluetooth(audioDevices));
                   case AudioDeviceInfo.TYPE_BUILTIN_SPEAKER:
                       return prepareAudioDevice(deviceInfo, getModeForSpeaker(audioDevices));
                   case AudioDeviceInfo.TYPE_WIRED_HEADSET:
                   case AudioDeviceInfo.TYPE_WIRED_HEADPHONES:
                   case AudioDeviceInfo.TYPE_USB_HEADSET: // OksaSDR
                       return prepareAudioDevice(deviceInfo, getModeForWired(audioDevices));
                   case AudioDeviceInfo.TYPE_BUILTIN_EARPIECE:
                       // It doesn't work when WIRED HEADPHONES are connected
                       // Earpiece has the lowest priority and setWiredHeadsetOn(boolean)
                       // method to force it is deprecated
                       Log.w(TAG, "Built in Earpiece may not work when "
                             + "Wired Headphones are connected");
                       return prepareAudioDevice(deviceInfo, AudioManager.MODE_IN_CALL);
                   case AudioDeviceInfo.TYPE_HDMI:
                   case AudioDeviceInfo.TYPE_HDMI_ARC:
                   case AudioDeviceInfo.TYPE_HDMI_EARC:
                       return prepareAudioDevice(deviceInfo, AudioManager.MODE_NORMAL);
                   default:
                       return false;
               }
           }
        }
        return false;
    }

    /**
     * Returns a device that can be set as a communication device, or null if no suitable device is
     * found
     */
    private static AudioDeviceInfo getValidCommunicationDevice(AudioDeviceInfo device) {
        if (device.isSink())
            return device;

        if (isBluetoothDevice(device)) {
            // For Bluetooth sources, get output device with the same type and address
            List<AudioDeviceInfo> communicationDevices = m_audioManager.getAvailableCommunicationDevices();
            for (AudioDeviceInfo communicationDevice : communicationDevices) {
                boolean isSameType = communicationDevice.getType() == device.getType();
                boolean isSameAddress = communicationDevice.getAddress().equals(device.getAddress());
                if (isSameType && isSameAddress)
                    return communicationDevice;
            }

            Log.w(TAG, "No matching bluetooth output device found for " + device);
        }

        return null;
    }

    private static boolean deviceIsCurrentCommunicationDevice(AudioDeviceInfo device) {
        if (m_currentCommunicationDeviceId == -1 || device == null) {
            return false;
        }

        return m_currentCommunicationDeviceId == device.getId();
    }

    @UsedFromNativeCode
    static void releaseAudioDevice(int id) {
        final AudioDeviceInfo[] devices = m_audioManager.getDevices(AudioManager.GET_DEVICES_ALL);
        for (AudioDeviceInfo device : devices) {
            if (device.getId() == id)
                releaseAudioDevice(device);
        }
    }

    /**
     * Revert any preparation done by prepareAudioDevice if no more streams exist that require them.
     */
    private static void releaseAudioDevice(AudioDeviceInfo deviceInfo) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            // If device was used as the communication device, it should be cleared
            AudioDeviceInfo communicationDevice = getValidCommunicationDevice(deviceInfo);
            if (!deviceIsCurrentCommunicationDevice(communicationDevice))
                return;

            if (m_communicationDeviceCounter.decrementAndGet() == 0) {
                m_audioManager.clearCommunicationDevice();
                m_currentCommunicationDeviceId = -1;
            }
        } else if (isBluetoothDevice(deviceInfo) && m_audioManager.isBluetoothScoOn()
                   && m_scoCounter.decrementAndGet() == 0) {
            m_audioManager.stopBluetoothSco();
            m_audioManager.setBluetoothScoOn(false);
        } else if (deviceInfo.getType() == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER
                   && m_speakerphoneCounter.decrementAndGet() == 0) {
            m_audioManager.setSpeakerphoneOn(false);
        }
    }

    private static boolean prepareAudioDevice(AudioDeviceInfo deviceInfo, int mode)
    {
        if (deviceInfo == null)
            return false;

        if (!appHoldsAudioMode) // OksaSDR
            m_audioManager.setMode(mode);

        boolean isSink = deviceInfo.isSink();
        if (isSink)
            m_currentOutputId = deviceInfo.getId();

        boolean isBluetoothDevice = isBluetoothDevice(deviceInfo);
        boolean isBluetoothSource = isBluetoothDevice && !isSink;
        boolean isCommunicationMode = (mode == AudioManager.MODE_IN_CALL
                                       || mode == AudioManager.MODE_IN_COMMUNICATION);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (!isBluetoothSource && !isCommunicationMode)
                return true;

            // For communication modes and Bluetooth sources, it's required to set a communication device
            AudioDeviceInfo communicationDevice = getValidCommunicationDevice(deviceInfo);
            if (communicationDevice == null) {
                Log.w(TAG, "No suitable communication device to set to enable communication via "
                           + deviceInfo.getId());
                return false;
            }

            if (deviceIsCurrentCommunicationDevice(communicationDevice)) {
                m_communicationDeviceCounter.incrementAndGet();
                return true;
            } else if (m_audioManager.setCommunicationDevice(communicationDevice)) {
                // NOTE: Keep track of communication devices we set, as it takes time for it to be
                // fully operational.
                // TODO: Other applications can set a different communication device, in which case
                // we should probably register a listener and clear our tracking when the
                // communication device unexpectedly changes
                m_currentCommunicationDeviceId = communicationDevice.getId();
                m_communicationDeviceCounter.set(1);
                return true;
            }

            return false;
        }

        boolean isSpeakerphoneDevice = deviceInfo.getType() == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER;

        if (isBluetoothSource && m_scoCounter.getAndIncrement() == 0) {
            m_audioManager.startBluetoothSco();
            m_audioManager.setBluetoothScoOn(true);
        } else if (isSpeakerphoneDevice
                   && m_speakerphoneCounter.getAndIncrement() == 0) {
            // TODO: Check if setting speakerphone is actually required for anything, it's not
            // recommended in Android docs.
            m_audioManager.setSpeakerphoneOn(true);
        }

        return true;
    }

    private static void streamSound()
    {
        byte data[] = new byte[m_bufferSize];
        while (m_isStreaming) {
            m_recorder.read(data, 0, m_bufferSize);
            m_streamPlayer.play();
            m_streamPlayer.write(data, 0, m_bufferSize);
            m_streamPlayer.stop();
        }
    }

    private static void startSoundStreaming(int inputId, int outputId)
    {
        if (m_isStreaming)
            stopSoundStreaming();

        m_recorder = new AudioRecord(MediaRecorder.AudioSource.DEFAULT, m_sampleRate, m_channels,
                                           m_audioFormat, m_bufferSize);
        m_streamPlayer = new AudioTrack(AudioManager.STREAM_MUSIC, m_sampleRate, m_channels,
                                           m_audioFormat, m_bufferSize, AudioTrack.MODE_STREAM);

        final AudioDeviceInfo[] devices = m_audioManager.getDevices(AudioManager.GET_DEVICES_ALL);
        for (AudioDeviceInfo deviceInfo : devices) {
            if (deviceInfo.getId() == outputId) {
                m_streamPlayer.setPreferredDevice(deviceInfo);
            } else if (deviceInfo.getId() == inputId) {
                m_recorder.setPreferredDevice(deviceInfo);
            }
        }

        m_recorder.startRecording();
        m_isStreaming = true;

        m_streamingThread = new Thread(new Runnable() {
            @Override
            public void run() {
                streamSound();
            }
        });

        m_streamingThread.start();
    }

    private static void stopSoundStreaming()
    {
        if (!m_isStreaming)
            return;

        m_isStreaming = false;
        try {
            m_streamingThread.join();
            m_streamingThread = null;
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
        m_recorder.stop();
        m_recorder.release();
        m_streamPlayer.release();
        m_streamPlayer = null;
        m_recorder = null;
    }
}
