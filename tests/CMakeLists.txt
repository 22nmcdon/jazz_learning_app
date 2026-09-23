# Engine tests. These link only against the engine and its JSON layer, so they
# build and run on a headless machine with no JUCE present - configure with
# -DJAZZ_BUILD_APP=OFF to do exactly that.

add_executable(jazz_core_tests
    TestMain.cpp
    ChordSymbolTests.cpp
    ChordIdentifierTests.cpp
    ScaleSuggesterTests.cpp
    VoicingAnalyzerTests.cpp
    LineAnalyzerTests.cpp
    LineWriterTests.cpp
    PracticeLogTests.cpp
    GrooveTests.cpp
    CompingTests.cpp
    ReharmonizerTests.cpp
    ChartTests.cpp
    ChartFormatsTests.cpp
    NoteInputTests.cpp
    EngineApiTests.cpp)

target_link_libraries(jazz_core_tests PRIVATE jazz::core_engine jazz::engine_api)
target_include_directories(jazz_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(jazz_core_tests PRIVATE /W4)
else()
    target_compile_options(jazz_core_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME core_engine COMMAND jazz_core_tests)
