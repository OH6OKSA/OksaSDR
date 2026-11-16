#include "MidiPairing.h"

#include "AndroidJni.h"
#include "MidiFramer.h"

#include "core/MidiControlManager.h"

#include <QJniEnvironment>
#include <QJniObject>
#include <QLoggingCategory>
#include <QStringList>

#include <mutex>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {

std::mutex g_framerMutex;
MidiFramer g_framer;

void JNICALL midiReceived(JNIEnv* env, jclass, jlong target, jbyteArray data, jint offset, jint count)
{
    if (target == 0 || count <= 0) {
        return;
    }
    QByteArray bytes(count, Qt::Uninitialized);
    env->GetByteArrayRegion(data, offset, count, reinterpret_cast<jbyte*>(bytes.data()));
    auto* manager = reinterpret_cast<MidiControlManager*>(target);
    std::lock_guard lock(g_framerMutex);
    for (const char c : std::as_const(bytes)) {
        g_framer.feed(static_cast<unsigned char>(c), [manager](const unsigned char* message, int size) {
            MidiControlManager::deliverPlatformMessage(manager, message, size);
        });
    }
}

QStringList devices()
{
    const QJniObject list = QJniObject::callStaticObjectMethod(
        kPlatformClass, "midiDevices", "(Landroid/content/Context;)[Ljava/lang/String;", androidContext().object());
    QStringList names;
    if (!list.isValid()) {
        return names;
    }
    QJniEnvironment env;
    const auto array = list.object<jobjectArray>();
    const jsize n = env->GetArrayLength(array);
    for (jsize i = 0; i < n; ++i) {
        QJniObject name = QJniObject::fromLocalRef(env->GetObjectArrayElement(array, i));
        names << name.toString();
    }
    return names;
}

} // namespace

// The app searches itself (Platform.scanBluetoothMidi): Android has no
// pairing screen for Bluetooth LE MIDI.
bool bluetoothMidiPairingAvailable()
{
    return true;
}

bool runningOnMac()
{
    return false;
}

namespace {
QString g_pairingProblem;
}

bool showBluetoothMidiPairing()
{
    g_pairingProblem = QJniObject::callStaticObjectMethod(kPlatformClass, "scanBluetoothMidi",
                                                          "(Landroid/content/Context;)Ljava/lang/String;",
                                                          androidContext().object())
                           .toString();
    qCInfo(lcMobile).noquote() << "MIDI: Bluetooth search"
                               << (g_pairingProblem.isEmpty() ? QStringLiteral("started") : g_pairingProblem);
    return g_pairingProblem.isEmpty();
}

QString bluetoothMidiPairingProblem()
{
    return g_pairingProblem;
}

bool bluetoothMidiSearching()
{
    return QJniObject::callStaticMethod<jboolean>(kPlatformClass, "midiScanning", "()Z");
}

void setupPlatformMidi()
{
    QJniEnvironment env;
    const JNINativeMethod methods[] = {
        {"midiReceived", "(J[BII)V", reinterpret_cast<void*>(midiReceived)},
    };
    if (!env.registerNativeMethods(kPlatformClass, methods, 1)) {
        qCWarning(lcMobile) << "MIDI: the receiver could not be registered; no controllers on this device";
        return;
    }
    MidiPlatformPorts ports;
    ports.list = devices;
    ports.open = [](const QString& name, MidiControlManager* target) {
        {
            std::lock_guard lock(g_framerMutex);
            g_framer = MidiFramer{};
        }
        return static_cast<bool>(QJniObject::callStaticMethod<jboolean>(
            kPlatformClass, "openMidi", "(Landroid/content/Context;Ljava/lang/String;J)Z", androidContext().object(),
            QJniObject::fromString(name).object<jstring>(), static_cast<jlong>(reinterpret_cast<quintptr>(target))));
    };
    ports.close = [] { QJniObject::callStaticMethod<void>(kPlatformClass, "closeMidi", "()V"); };
    ports.isOpen = [] { return static_cast<bool>(QJniObject::callStaticMethod<jboolean>(kPlatformClass, "midiOpen", "()Z")); };
    MidiControlManager::setPlatformPorts(std::move(ports));
}

} // namespace AetherSDR::mobile
