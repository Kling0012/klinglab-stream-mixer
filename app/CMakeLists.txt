juce_add_gui_app(KlinglabStreamMixer
    PRODUCT_NAME "Klinglab Stream Mixer"
    COMPANY_NAME "Klinglab"
    BUNDLE_ID "site.klinglab.streammixer"
    VERSION "${PROJECT_VERSION}"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "マイクの声を配信とヘッドホンへ送るために、マイクを使用します。"
    PLIST_TO_MERGE "<plist version=\"1.0\"><dict><key>LSMinimumSystemVersion</key><string>14.4</string><key>NSAudioCaptureUsageDescription</key><string>選んだアプリの音声を取り込み、配信とヘッドホンへ送るために使用します。</string><key>NSHumanReadableCopyright</key><string>Copyright © 2026 Klinglab. Licensed under AGPL-3.0-only.</string></dict></plist>"
)

target_sources(KlinglabStreamMixer PRIVATE
    src/Main.cpp
    src/engine/CoreAudioBackend.mm
    src/engine/ClockBridge.h
    src/io/SharedMemorySender.cpp
    src/engine/AudioEngine.h
    src/engine/Router.h
    src/engine/Meter.h
    src/engine/Fader.h
    src/capture/ProcessTapCapture.h
    src/capture/ProcessTapCapture.mm
    src/ui/AppSelectionPanel.cpp
    src/ui/AppSelectionPanel.h
    src/project/ProjectStore.cpp
    src/dsp/NR.h
    src/dsp/NR.cpp
    src/dsp/NRRender.h
    src/dsp/NRRender.cpp
    src/ui/NRPanel.h
    src/ui/NRPanel.cpp
    src/dsp/Loudness.h
    src/dsp/Loudness.cpp
    src/dsp/Limiter.h
    src/dsp/Limiter.cpp
    src/dsp/LoudRender.h
    src/dsp/LoudRender.cpp
    src/ui/LoudPanel.h
    src/ui/LoudPanel.cpp
    src/plugins/PluginHost.h
    src/plugins/PluginHost.cpp
    src/plugins/Scanner.h
    src/plugins/Scanner.cpp
    src/ui/PluginPanel.h
    src/ui/PluginPanel.cpp
    src/io/SharedMemorySender.cpp
    src/io/SoundLibrary.h
    src/io/SoundLibrary.cpp
    src/ui/SoundPanel.h
    src/ui/SoundPanel.cpp
    src/project/ProjectStore.h
    src/project/Settings.cpp
    src/project/Settings.h
    src/project/Hotkeys.h
    src/project/Hotkeys.cpp
    src/remote/WebSocketServer.h
    src/remote/WebSocketServer.cpp
    src/ui/HelpPanel.cpp
    src/ui/HelpPanel.h
    src/ui/WindowCapture.mm
    src/ui/Guidance.cpp
    src/ui/Guidance.h
    src/ui/MainComponent.cpp
    src/ui/MainComponent.h
    src/ui/Panels.cpp
    src/ui/Panels.h
)

set(KSM_STREAMDECK_PACKAGE "" CACHE FILEPATH "Packaged Stream Deck plugin to bundle")
if(KSM_STREAMDECK_PACKAGE)
    if(NOT EXISTS "${KSM_STREAMDECK_PACKAGE}")
        message(FATAL_ERROR "Stream Deck package does not exist: ${KSM_STREAMDECK_PACKAGE}")
    endif()
    target_sources(KlinglabStreamMixer PRIVATE "${KSM_STREAMDECK_PACKAGE}")
    set_source_files_properties("${KSM_STREAMDECK_PACKAGE}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources HEADER_FILE_ONLY TRUE)
else()
    message(WARNING "Stream Deck plugin is not bundled; use scripts/mac-build.sh for a complete app")
endif()

set(KSM_DISTRIBUTION_FILES "${CMAKE_SOURCE_DIR}/scripts/uninstall.command" "${CMAKE_SOURCE_DIR}/scripts/installer/user-home.sh" "${CMAKE_SOURCE_DIR}/LICENSE")
target_sources(KlinglabStreamMixer PRIVATE ${KSM_DISTRIBUTION_FILES})
set_source_files_properties(${KSM_DISTRIBUTION_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources HEADER_FILE_ONLY TRUE)

file(GLOB KSM_MANUAL CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/docs/manual/*.md")
target_sources(KlinglabStreamMixer PRIVATE ${KSM_MANUAL})
set_source_files_properties(${KSM_MANUAL} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources/manual HEADER_FILE_ONLY TRUE)
set(KSM_OBS_PACKAGE "" CACHE FILEPATH "Packaged OBS plugin to bundle")
if(KSM_OBS_PACKAGE AND EXISTS "${KSM_OBS_PACKAGE}")
    target_sources(KlinglabStreamMixer PRIVATE "${KSM_OBS_PACKAGE}")
    set_source_files_properties("${KSM_OBS_PACKAGE}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources HEADER_FILE_ONLY TRUE)
else()
    message(WARNING "OBS plugin is not bundled; use scripts/mac-build.sh for a complete app")
endif()

target_compile_definitions(KlinglabStreamMixer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:KlinglabStreamMixer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
)

enable_language(OBJCXX)
set_target_properties(KlinglabStreamMixer PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES)
target_link_libraries(KlinglabStreamMixer PRIVATE
    "-framework Carbon"
    "-framework CoreAudio"
    "-framework AudioUnit"
    "-framework CoreAudioKit"
    "-framework AVFoundation"
    "-framework AppKit"
    juce::juce_gui_basics
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
