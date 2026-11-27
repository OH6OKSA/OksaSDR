# aether-mobile — touch client that links libaethercore in-process and talks to
# the radio directly (no PC or aetherd in between). Configured from the root
# CMakeLists.txt only when AETHER_MOBILE is ON. See mobile/README.md.

# QuickDialogs2: the Log tab's file picker (ADIF import).
find_package(Qt6 6.12 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 GuiPrivate)

qt_standard_project_setup(REQUIRES 6.12)

# Core reads ":/..." assets (demo voice, band plans) from the executable's
# resources, like the desktop app. radar.qrc is map-only and stays out.
qt_add_resources(AETHER_MOBILE_RESOURCES ${CMAKE_SOURCE_DIR}/resources/resources.qrc)

qt_add_executable(aether-mobile
    src/main.cpp
    src/MobileEngine.cpp
    src/MobileEngine.h
    src/RadioListModel.cpp
    src/RadioListModel.h
    src/SpectrumItem.cpp
    src/SpectrumItem.h
    src/SpectrumPalette.h
    src/NoiseFloorTracker.h
    src/Spectrum3DItem.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/MemoryCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/ModeFilterPresets.cpp
    src/Spectrum3DItem.h
    src/ScreenAwake.h
    src/LogBuffer.cpp
    src/DevLink.cpp
    src/ShotPlan.cpp
    src/StallWatch.cpp
    src/ShotPlan.h
    src/StallWatch.h
    src/EchoAudit.cpp
    src/TrackpadScrollPhases.cpp
    src/QsoLog.cpp
    src/QsoLog.h
    src/Ft8Engine.cpp
    src/Ft8Engine.h
    src/NtpClock.cpp
    src/NtpClock.h
    src/MobileEngineDigi.cpp
    src/MobileEngineTextModes.cpp
    src/MobileEngineRade.cpp
    src/MobileEngineOnlineLog.cpp
    src/MobileEngineQrzLog.cpp
    src/MobileEnginePota.cpp
    src/MobileEngineUpdate.cpp
    src/MobileWorkspace.cpp
    src/MobileWorkspace.h
    # The desktop's Workspace Canvas model (RFC #4887), widget-free: the own
    # layouts' rules and stored form (roadmap 113).
    ${CMAKE_SOURCE_DIR}/src/gui/workspace/WorkspaceGeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/workspace/CanvasInteraction.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/workspace/WorkspaceDocument.cpp
    src/MobileEngineVoiceLog.cpp
    src/MobileEngineSmartLink.cpp
    src/MobileEngineStart.cpp
    src/MobileEngineSharedAdif.cpp
    src/MobileEngineLogger32.cpp
    src/MobileEngineLoggerUdp.cpp
    src/MobileEngineSetups.cpp
    src/MobileEngineAgcAuto.cpp
    src/MobileEngineMidi.cpp
    src/MobileEngineUlanzi.cpp
    src/MobileEnginePttKey.cpp
    src/MobileEnginePicker.cpp
    src/MobileEngineSpotAlerts.cpp
    src/MobileEngineSpotFeeds.cpp
    src/MiniMqtt.cpp
    src/MiniMqtt.h
    src/AdifDxcc.h
    src/LocalNotify.h
    src/LocalNotify.cpp
    src/MobileEngineDigitalMax.cpp
    src/MobileEngineAmp.cpp
    src/MobileEngineKpa.cpp
    src/MobileEngineElecraft.cpp
    src/Kpa1500Reader.cpp
    src/Kpa1500Reader.h
    src/MobileEngine4o3a.cpp
    src/MobileEngineDoctor.cpp
    src/MobileEngineFreeDv.cpp
    src/PskReporterUpload.cpp
    src/TrackpadScrollPhases.h
    src/EchoAudit.h
    src/DevLink.h
    src/LogBuffer.h
    src/SecureStore.h
    src/AudioSession.h
    src/ShareText.h
    src/Haptics.h
    src/TextScale.h
    src/DeviceId.h
    src/SideKeys.h
    src/KeyboardGuard.h
    src/MidiPairing.h
    src/VoiceInput.h
    src/CallsignSpeech.h
    src/CallsignSpeech.cpp
    ${AETHER_MOBILE_RESOURCES}
)
if(IOS)
    target_sources(aether-mobile PRIVATE ios/ScreenAwake.mm ios/SecureStore.mm ios/AudioSession.mm ios/ShareText.mm
                                         ios/SideKeys.mm ios/Haptics.mm ios/TextScale.mm ios/KeyboardGuard.mm
                                         ios/DeviceId.mm ios/MidiPairing.mm ios/VoiceInput.mm ios/LocalNotify.mm
                                         src/VoskSpeech.h src/VoskSpeech.cpp)
    target_link_libraries(aether-mobile PRIVATE "-framework Security" "-framework AVFAudio" "-framework GameController"
                                                "-framework MessageUI" "-framework CoreAudioKit" "-framework Speech"
                                                "-framework UserNotifications")
    # Vosk for logging by voice (roadmap 81): Alphacephei's iOS libvosk
    # (static, arm64; BLAS and LAPACK from Accelerate) and the English model
    # in the bundle (vosk/en-us), as the iOS workflow unpacks them. Without
    # it the voice button uses Apple's recogniser.
    set(AETHER_IOS_VOSK_DIR "" CACHE PATH "libvosk.a (arm64), en-us/ (the model) and LICENSE for iOS")
    if(AETHER_IOS_VOSK_DIR)
        target_compile_definitions(aether-mobile PRIVATE AETHER_IOS_VOSK=1)
        target_link_libraries(aether-mobile PRIVATE "${AETHER_IOS_VOSK_DIR}/libvosk.a" "-framework Accelerate")
        file(GLOB_RECURSE _vosk_files RELATIVE "${AETHER_IOS_VOSK_DIR}/en-us" "${AETHER_IOS_VOSK_DIR}/en-us/*")
        foreach(_f IN LISTS _vosk_files)
            get_filename_component(_d "${_f}" DIRECTORY)
            set(_at "Resources/vosk/en-us")
            if(_d)
                set(_at "${_at}/${_d}")
            endif()
            target_sources(aether-mobile PRIVATE "${AETHER_IOS_VOSK_DIR}/en-us/${_f}")
            set_source_files_properties("${AETHER_IOS_VOSK_DIR}/en-us/${_f}" PROPERTIES MACOSX_PACKAGE_LOCATION "${_at}")
        endforeach()
        target_sources(aether-mobile PRIVATE "${AETHER_IOS_VOSK_DIR}/LICENSE")
        set_source_files_properties("${AETHER_IOS_VOSK_DIR}/LICENSE" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/vosk")
        list(LENGTH _vosk_files _n)
        message(STATUS "Vosk for iOS: ${_n} model files from ${AETHER_IOS_VOSK_DIR}")
    else()
        message(WARNING "AETHER_IOS_VOSK_DIR not set: the voice button uses Apple's recogniser")
    endif()
elseif(ANDROID)
    target_sources(aether-mobile PRIVATE android/AndroidJni.h android/ScreenAwake.cpp android/SecureStore.cpp
                                         src/AudioSession.cpp android/ShareText.cpp src/SideKeys.cpp android/KeyLog.cpp
                                         android/Haptics.cpp android/TextScale.cpp src/DeviceId.cpp src/KeyboardGuard.cpp
                                         android/MidiPairing.cpp android/VoiceInput.cpp
                                         src/VoskSpeech.h src/VoskSpeech.cpp)
    target_include_directories(aether-mobile PRIVATE src)
else()
    target_sources(aether-mobile PRIVATE src/ScreenAwake.cpp src/SecureStore.cpp src/AudioSession.cpp src/ShareText.cpp
                                         src/SideKeys.cpp src/Haptics.cpp src/TextScale.cpp src/KeyboardGuard.cpp
                                         src/DeviceId.cpp src/MidiPairing.cpp src/VoiceInput.cpp
                                         src/VoskSpeech.h src/VoskSpeech.cpp)
    target_link_libraries(aether-mobile PRIVATE ${CMAKE_DL_LIBS})
    # The desktop's Secret Service (GNOME Keyring, KWallet, KeePassXC) for
    # the secrets, through qtkeychain (roadmap 91; the AppImage builds it
    # with scripts/setup/setup-qtkeychain.sh). Without it they stay in a file
    # only this user can read, as in the development build.
    if(Qt6Keychain_FOUND)
        target_compile_definitions(aether-mobile PRIVATE OKSASDR_KEYCHAIN=1)
        target_link_libraries(aether-mobile PRIVATE Qt6Keychain::Qt6Keychain)
        message(STATUS "OksaSDR: secrets in the Secret Service (qtkeychain)")
    else()
        message(STATUS "OksaSDR: no qtkeychain, secrets in a file only this user can read")
    endif()
    if(NOT APPLE)
        # The window's icon (the AppImage's entry and icon: mobile/linux).
        qt_add_resources(aether-mobile "linux_icon" PREFIX "/" BASE linux FILES linux/oksasdr.png)
        # The app's own function names in a stalled thread's stack
        # (StallWatch), kept in the dynamic symbols the AppImage's strip
        # leaves (roadmap 91 f).
        if(UNIX)
            target_link_options(aether-mobile PRIVATE -rdynamic)
        endif()
    endif()
endif()

# Must precede qt_add_qml_module, which reads it when writing the qmldir.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(aether-mobile
    URI AetherMobile
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/ConnectPage.qml
        qml/OperatePage.qml
        qml/Theme.qml
        qml/FrequencyDialog.qml
        qml/LogPage.qml
        qml/PanadapterView.qml
        qml/AppButton.qml
        qml/MenuIcon.qml
        qml/TopButton.qml
        qml/CwKeyboardPaddles.qml
        qml/ControlsPanel.qml
        qml/MeterView.qml
        qml/AnalogGauge.qml
        qml/BarGauge.qml
        qml/BackspaceIcon.qml
        qml/PasswordField.qml
        qml/OperateKeys.qml
        qml/MeterMarks.js
        qml/TxControls.qml
        qml/EssbControls.qml
        qml/RxControls.qml
        qml/SliceBar.qml
        qml/SliceMenu.qml
        qml/SlicePowerPopup.qml
        qml/CwControls.qml
        qml/DisplayControls.qml
        qml/SpotsControls.qml
        qml/CwMemoryEditor.qml
        qml/TextMacroEditor.qml
        qml/DvkSlotMenu.qml
        qml/ModeButton.qml
        qml/FilterChooser.qml
        qml/ChooserPopup.qml
        qml/AgcChooser.qml
        qml/RepeatButton.qml
        qml/AppSlider.qml
        qml/FineSlider.qml
        qml/HintsDialog.qml
        qml/PopupScroll.qml
        qml/PanelHeader.qml
        qml/PanelContent.qml
        qml/PageScroll.qml
        qml/PttButton.qml
        qml/NetworkBadge.qml
        qml/NetworkDialog.qml
        qml/RadioSetupPage.qml
        qml/BigButtonsView.qml
        qml/SetupSuggestion.qml
        qml/UpdateOffer.qml
        qml/LayoutCanvas.qml
        qml/CanvasBlock.qml
        qml/FrequencyBlock.qml
        qml/StepSizeBlock.qml
        qml/ModeBlock.qml
        qml/InfoBlock.qml
        qml/ButtonGridBlock.qml
        qml/SliceChip.qml
        qml/SlicesBlock.qml
        qml/MeterBlock.qml
        qml/AddressDialog.qml
        qml/Caret.qml
        qml/MeterBadge.qml
        qml/InfoPanel.qml
        qml/EqControls.qml
        qml/ProfileControls.qml
        qml/MemoryControls.qml
        qml/LogControls.qml
        qml/LoggingSetup.qml
        qml/AppSetup.qml
        qml/SpotsSetup.qml
        qml/FtxSetup.qml
        qml/CwKeyingSetup.qml
        qml/PttKeySetup.qml
        qml/SpotAlertBanner.qml
        qml/VoiceButton.qml
        qml/OffsetEditor.qml
        qml/DigiControls.qml
        qml/FreeDvControls.qml
        qml/RttyControls.qml
        qml/AmpControls.qml
        qml/Kat500Controls.qml
        qml/GeniusControls.qml
        qml/MacrosPanel.qml
        qml/AppScrollBar.qml
        qml/AppComboBox.qml
        qml/PhoneOperatePage.qml
        qml/StationsDialog.qml
        qml/StepButton.qml
        qml/SectionTab.qml
)

# The generated QML type registrations include the headers by bare name.
target_include_directories(aether-mobile PRIVATE src)

# FT8 / FT4 (ft8_lib, MIT; third_party/ft8_lib). Its KISS FFT exports the
# same names as RADE's copy in libaethercore, so they are renamed here.
set(FT8LIB_DIR ${CMAKE_SOURCE_DIR}/third_party/ft8_lib)
add_library(ft8lib STATIC
    ${FT8LIB_DIR}/ft8/constants.c
    ${FT8LIB_DIR}/ft8/crc.c
    ${FT8LIB_DIR}/ft8/decode.c
    ${FT8LIB_DIR}/ft8/encode.c
    ${FT8LIB_DIR}/ft8/ldpc.c
    ${FT8LIB_DIR}/ft8/message.c
    ${FT8LIB_DIR}/ft8/text.c
    ${FT8LIB_DIR}/common/monitor.c
    ${FT8LIB_DIR}/fft/kiss_fft.c
    ${FT8LIB_DIR}/fft/kiss_fftr.c
)
target_include_directories(ft8lib PUBLIC ${FT8LIB_DIR})
target_compile_definitions(ft8lib PRIVATE
    kiss_fft=ftx_kiss_fft
    kiss_fft_alloc=ftx_kiss_fft_alloc
    kiss_fft_cleanup=ftx_kiss_fft_cleanup
    kiss_fft_next_fast_size=ftx_kiss_fft_next_fast_size
    kiss_fft_stride=ftx_kiss_fft_stride
    kiss_fftr=ftx_kiss_fftr
    kiss_fftr_alloc=ftx_kiss_fftr_alloc
    kiss_fftri=ftx_kiss_fftri
)
if(MSVC)
    target_compile_options(ft8lib PRIVATE /w)
else()
    target_compile_options(ft8lib PRIVATE -w)
endif()
target_link_libraries(aether-mobile PRIVATE ft8lib)

target_link_libraries(aether-mobile PRIVATE
    aethercore
    Qt6::Quick
    Qt6::QuickControls2
    # QRhi (<rhi/qrhi.h>) for the GPU 3D view.
    Qt6::GuiPrivate
)

# Precompiled shaders for Spectrum3DItem (SPIR-V, GLSL, HLSL and Metal in
# one .qsb): the desktop's resources/shaders/dss_mesh.{vert,frag}, copied
# unchanged. Regenerate after editing shaders/*.vert|frag with
#   qsb --glsl "300 es,150" --hlsl 50 --msl 12 -o X.qsb X
# so the iOS build needs no Qt ShaderTools.
# The spot alert's sound (roadmap 132): two short tones made for the app.
qt_add_resources(aether-mobile "alert_sound" PREFIX "/" BASE . FILES sounds/spot-alert.wav)
qt_add_resources(aether-mobile "spectrum3d_shaders"
    PREFIX "/"
    FILES
        shaders/dss_mesh.vert.qsb
        shaders/dss_mesh.frag.qsb
)

# Symbol fonts for Android, whose own Noto Sans Symbols is a subset without
# ✎ ✕ ⌥ ⇄ and others (they drew as boxes): subsets of Noto Sans Symbols 2
# (Miscellaneous Technical, Geometric Shapes, Miscellaneous Symbols,
# Dingbats, Symbols and Arrows) and Noto Sans Math (Arrows, Mathematical
# Operators), SIL OFL 1.1 (fonts/OFL.txt). Also in the desktop build, for
# testing with AETHER_SYMBOL_FONTS=1.
if(NOT IOS AND NOT APPLE)
    qt_add_resources(aether-mobile "symbol_fonts"
        PREFIX "/"
        FILES
            fonts/NotoSansSymbols2-Subset.ttf
            fonts/NotoSansMath-Subset.ttf
    )
endif()

if(MSVC)
    target_compile_options(aether-mobile PRIVATE /W3 /Zc:__cplusplus /permissive- /utf-8)
else()
    target_compile_options(aether-mobile PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Signing identity of the iOS app. The bundle ID must be one the signing
# Apple team owns; the build number must grow with every TestFlight upload.
set(AETHER_MOBILE_BUNDLE_ID "com.aethersdr.mobile" CACHE STRING "iOS bundle identifier of aether-mobile")
set(AETHER_MOBILE_BUILD_NUMBER "1" CACHE STRING "iOS CFBundleVersion of aether-mobile")
set(AETHER_MOBILE_TEAM_ID "" CACHE STRING "Apple Developer Team ID that signs aether-mobile (empty: unsigned)")
# Android's versionCode must grow with every APK, while the number the app
# shows is the beta's there (android-beta-N shows N), so the two are apart.
set(AETHER_MOBILE_VERSION_CODE "" CACHE STRING "Android versionCode of aether-mobile (empty: the build number)")
# The build number in the app too (Log, developer link), to tell builds apart.
target_compile_definitions(aether-mobile PRIVATE AETHER_MOBILE_BUILD_NUMBER="${AETHER_MOBILE_BUILD_NUMBER}")
# The newer-version check (roadmap 111, MobileEngineUpdate.cpp): the GitHub
# APK only. TestFlight, the App Store and Google Play update their builds, so
# a Play build turns it off.
if(ANDROID)
    set(_aether_update_check_default ON)
else()
    set(_aether_update_check_default OFF)
endif()
option(AETHER_MOBILE_UPDATE_CHECK "Check GitHub for a newer Android beta (the GitHub APK; off for a Play build)" ${_aether_update_check_default})
if(AETHER_MOBILE_UPDATE_CHECK)
    target_compile_definitions(aether-mobile PRIVATE AETHER_MOBILE_UPDATE_CHECK)
endif()

if(ANDROID)
    set(_aether_version_code "${AETHER_MOBILE_VERSION_CODE}")
    if(NOT _aether_version_code)
        set(_aether_version_code "${AETHER_MOBILE_BUILD_NUMBER}")
    endif()
    # The versionCode (the CI run) in the app too: the update check offers a
    # newer build of the same beta number (roadmap 133: a preview's APK and
    # the released one are both "beta N").
    target_compile_definitions(aether-mobile PRIVATE AETHER_MOBILE_ANDROID_RUN=${_aether_version_code})
    # Manifest (package fi.oh6oksa.oksasdr, permissions) and later Java glue.
    set_target_properties(aether-mobile PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        QT_ANDROID_APP_NAME "OksaSDR"
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 35
        QT_ANDROID_VERSION_CODE "${_aether_version_code}"
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
    )
    # Qt loads OpenSSL at run time for TLS (SmartLink, FlexRadio release
    # checks); Android has none, so the APK carries it. Point this at a
    # checkout of https://github.com/KDAB/android_openssl (Qt's prebuilt libs).
    set(AETHER_ANDROID_OPENSSL_DIR "" CACHE PATH "KDAB android_openssl checkout (OpenSSL 3 for the APK)")
    if(AETHER_ANDROID_OPENSSL_DIR)
        set(_ssl "${AETHER_ANDROID_OPENSSL_DIR}/ssl_3/${CMAKE_ANDROID_ARCH_ABI}")
        set_property(TARGET aether-mobile APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
            "${_ssl}/libcrypto_3.so" "${_ssl}/libssl_3.so")
    else()
        message(WARNING "AETHER_ANDROID_OPENSSL_DIR not set: the APK has no OpenSSL, so SmartLink cannot sign in")
    endif()
    # Vosk for logging by voice (roadmap 81): libvosk from the release's
    # vosk-android zip (<dir>/<abi>/libvosk.so), opened at run time; its
    # English model goes in as assets (android/assets/vosk/en-us, put there
    # by CI, not in git). Without them the voice button is not offered.
    set(AETHER_ANDROID_VOSK_DIR "" CACHE PATH "Unpacked vosk-android release zip (libvosk.so per ABI)")
    if(AETHER_ANDROID_VOSK_DIR)
        set_property(TARGET aether-mobile APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
            "${AETHER_ANDROID_VOSK_DIR}/${CMAKE_ANDROID_ARCH_ABI}/libvosk.so")
    else()
        message(WARNING "AETHER_ANDROID_VOSK_DIR not set: the APK has no Vosk, so voice entry is not offered")
    endif()
    # USB-C headsets: Qt 6.12.0 leaves them out of its Android device list
    # (android/qt-patches). The patched class goes into the Qt install's jar,
    # which the APK packaging takes.
    if(Qt6_VERSION VERSION_EQUAL 6.12.0)
        file(GLOB _android_jars "${ANDROID_SDK_ROOT}/platforms/android-*/android.jar")
        list(SORT _android_jars)
        list(POP_BACK _android_jars _android_jar)
        execute_process(COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/android/patch-qt-usb-audio.sh"
                                "${QT6_INSTALL_PREFIX}/jar" "${_android_jar}"
                        RESULT_VARIABLE _qt_usb_patch
                        OUTPUT_VARIABLE _qt_usb_patch_out
                        ERROR_VARIABLE _qt_usb_patch_out)
        if(_qt_usb_patch EQUAL 0)
            message(STATUS "Qt USB headset patch: ${_qt_usb_patch_out}")
        else()
            message(WARNING "Qt USB headset patch failed, USB-C headsets stay unused: ${_qt_usb_patch_out}")
        endif()
    else()
        message(WARNING "android/qt-patches is for Qt 6.12.0, not ${Qt6_VERSION}: USB-C headsets stay unused")
    endif()
endif()

if(IOS)
    # Audio goes through the Darwin multimedia backend; the FFmpeg plugin
    # would need FFmpeg frameworks the app neither ships nor uses.
    qt_import_plugins(aether-mobile EXCLUDE Qt6::QFFmpegMediaPlugin)
    target_sources(aether-mobile PRIVATE ios/Assets.xcassets)
    set_source_files_properties(ios/Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(aether-mobile PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "${AETHER_MOBILE_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_NAME "OksaSDR"
        MACOSX_BUNDLE_BUNDLE_VERSION "${AETHER_MOBILE_BUILD_NUMBER}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${AETHER_MOBILE_BUNDLE_ID}"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${AETHER_MOBILE_BUILD_NUMBER}"
        XCODE_ATTRIBUTE_MARKETING_VERSION "${PROJECT_VERSION}"
    )
    if(AETHER_MOBILE_TEAM_ID)
        set_target_properties(aether-mobile PROPERTIES
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${AETHER_MOBILE_TEAM_ID}"
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        )
    endif()
endif()
