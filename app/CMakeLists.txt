# Platform Shell: MIDI I/O, file import/export and app lifecycle.
#
# This is the only target that knows about devices and platforms. It is a JUCE
# "GUI Application" (standalone) target - a future AUv3/VST3 target would be a
# sibling of this directory reusing jazz::core_engine, which is why no engine
# code lives here.

juce_add_gui_app(JazzLearningApp
    PRODUCT_NAME "Jazz Learning App"
    COMPANY_NAME "Jazz Learning App"
    BUNDLE_ID com.jazzlearningapp.poc
    VERSION ${PROJECT_VERSION}

    # iOS/Android: Bluetooth MIDI keyboards need the permission prompt; audio
    # input does not, because pitch detection is deliberately out of scope.
    MICROPHONE_PERMISSION_ENABLED FALSE
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "Connect a Bluetooth MIDI keyboard to play along with the chart."

    IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationPortrait
                               UIInterfaceOrientationLandscapeLeft
                               UIInterfaceOrientationLandscapeRight
    IPAD_SCREEN_ORIENTATIONS   UIInterfaceOrientationPortrait
                               UIInterfaceOrientationLandscapeLeft
                               UIInterfaceOrientationLandscapeRight)

# The page is the UI, so it ships inside the binary: no files beside the app,
# and no local server to start.
#
# The page, the band's recorded instruments, and pdf.js. Everything but the page
# lives in assets/ rather than in web/ or app/ because both shells need it: the
# app compiles it in here, and web/build.sh copies the same files next to the
# page.
#
# pdf.js is 1.4MB of the binary, which is most of what assets/ weighs - it was
# 296KB before. That is the price of reading a PDF without a C++ PDF library and
# without running somebody else's script off a CDN, and it was chosen rather
# than arrived at. The samples are recordings and cannot shrink; this one could
# be dropped again by giving the app a native extractor instead, which is a much
# larger change than it sounds.
juce_add_binary_data(JazzWebUiData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../web/index.html
        ${CMAKE_CURRENT_SOURCE_DIR}/../assets/bass-upright.wav
        ${CMAKE_CURRENT_SOURCE_DIR}/../assets/bass-electric.wav
        ${CMAKE_CURRENT_SOURCE_DIR}/../assets/piano-grand.wav
        ${CMAKE_CURRENT_SOURCE_DIR}/../assets/pdf.min.js
        ${CMAKE_CURRENT_SOURCE_DIR}/../assets/pdf.worker.min.js)

target_sources(JazzLearningApp PRIVATE
    src/Main.cpp
    src/ElectricPiano.cpp
    src/MidiDeviceInput.cpp
    src/WebUi.cpp)

target_include_directories(JazzLearningApp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(JazzLearningApp PRIVATE
    JazzWebUiData
    jazz::engine_api
    jazz::core_engine
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_compile_definitions(JazzLearningApp PRIVATE
    # The UI is a web page in a webview, so this one is not optional.
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2=1
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:JazzLearningApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:JazzLearningApp,JUCE_VERSION>")

# On Linux, JUCE's webview is WebKitGTK and JUCE leaves the flags to the project.
# macOS and Windows need nothing here: WKWebView is part of the system, and
# WebView2 comes in through JUCE_USE_WIN_WEBVIEW2 above.
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)

    # 4.1 is what current distributions ship; 4.0 is kept for older ones.
    pkg_check_modules(JAZZ_WEBKIT QUIET webkit2gtk-4.1)

    if(NOT JAZZ_WEBKIT_FOUND)
        pkg_check_modules(JAZZ_WEBKIT QUIET webkit2gtk-4.0)
    endif()

    if(NOT JAZZ_WEBKIT_FOUND)
        message(FATAL_ERROR
            "The app's UI is a web page in a webview, which on Linux needs WebKitGTK.\n"
            "Install libwebkit2gtk-4.1-dev (or libwebkit2gtk-4.0-dev) and libgtk-3-dev.")
    endif()

    target_include_directories(JazzLearningApp PRIVATE ${JAZZ_WEBKIT_INCLUDE_DIRS})
    target_link_libraries(JazzLearningApp PRIVATE ${JAZZ_WEBKIT_LIBRARIES})
    target_compile_options(JazzLearningApp PRIVATE ${JAZZ_WEBKIT_CFLAGS_OTHER})
endif()
