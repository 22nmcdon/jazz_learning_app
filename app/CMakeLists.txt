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

target_sources(JazzLearningApp PRIVATE
    src/Main.cpp
    src/MidiDeviceInput.cpp)

target_include_directories(JazzLearningApp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(JazzLearningApp PRIVATE
    jazz::shared_ui
    jazz::core_engine
    juce::juce_audio_devices
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_compile_definitions(JazzLearningApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:JazzLearningApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:JazzLearningApp,JUCE_VERSION>")
