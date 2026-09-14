# Shared UI Component Library: one set of size-class-aware components used on
# every platform. Depends on the Core Engine for data and on JUCE for drawing -
# never the other way round, and never on the platform shell.

add_library(jazz_shared_ui STATIC
    src/SizeClass.cpp
    src/ChartView.cpp
    src/OnScreenKeyboard.cpp
    src/ScaleSuggestionPanel.cpp
    src/ReharmonizationPanel.cpp
    src/VoicingFeedbackPanel.cpp
    src/MainComponent.cpp)

add_library(jazz::shared_ui ALIAS jazz_shared_ui)

target_include_directories(jazz_shared_ui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(jazz_shared_ui
    PUBLIC
        jazz::core_engine
        juce::juce_gui_basics
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

target_compile_definitions(jazz_shared_ui
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=0)
