# The engine's answers as JSON: one wire format, shared by every shell.
#
# Pure C++17 like the Core Engine it sits on - no JUCE, no Emscripten. That is
# what lets the browser shell and the JUCE app hand their UI the same answers.

# One file per question the wire answers, rather than one file for all of them.
# `src/Wire.h` holds only what more than one of the four needs - the bar for
# living there is two callers in two different files, which is why it is a
# header of small inline helpers and not a fifth translation unit.
add_library(jazz_engine_api STATIC
    src/ChartApi.cpp
    src/VoicingApi.cpp
    src/CompingApi.cpp
    src/SoloApi.cpp)

add_library(jazz::engine_api ALIAS jazz_engine_api)

target_include_directories(jazz_engine_api
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(jazz_engine_api PUBLIC jazz::core_engine)

target_compile_features(jazz_engine_api PUBLIC cxx_std_17)

# The same warnings the Core Engine builds with, for the same reason. This was
# the one target in the repo with none: the widest surface anything crosses,
# compiled in silence, while the engine beside it ran
# -Wall -Wextra -Wpedantic and the app linked JUCE's recommended set. An unused
# local here should be found by the build, not by an audit.
if(MSVC)
    target_compile_options(jazz_engine_api PRIVATE /W4)
else()
    target_compile_options(jazz_engine_api PRIVATE -Wall -Wextra -Wpedantic)
endif()
