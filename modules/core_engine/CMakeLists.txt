# Core Engine: chord parsing, scale suggestion, reharmonisation, voicing
# analysis. Pure C++17 - no JUCE, no GUI, no platform APIs.
#
# Do not add a JUCE dependency to this target. Keeping it free of JUCE is what
# lets the same engine back a future AUv3/VST3 plugin target, and what lets the
# unit tests run anywhere.

add_library(jazz_core_engine STATIC
    src/Pitch.cpp
    src/ChordSymbol.cpp
    src/ChordIdentifier.cpp
    src/Scale.cpp
    src/ScaleSuggester.cpp
    src/Voicing.cpp
    src/Rhythm.cpp
    src/Comping.cpp
    src/VoicingAnalyzer.cpp
    src/LineAnalyzer.cpp
    src/PracticeLog.cpp
    src/Chart.cpp
    src/ChartFormats.cpp
    src/Reharmonizer.cpp
    src/NoteInput.cpp)

add_library(jazz::core_engine ALIAS jazz_core_engine)

target_include_directories(jazz_core_engine
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(jazz_core_engine PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(jazz_core_engine PRIVATE /W4)
else()
    target_compile_options(jazz_core_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
