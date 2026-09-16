# The engine's answers as JSON: one wire format, shared by every shell.
#
# Pure C++17 like the Core Engine it sits on - no JUCE, no Emscripten. That is
# what lets the browser shell and the JUCE app hand their UI the same answers.

add_library(jazz_engine_api STATIC
    src/EngineApi.cpp)

add_library(jazz::engine_api ALIAS jazz_engine_api)

target_include_directories(jazz_engine_api
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(jazz_engine_api PUBLIC jazz::core_engine)

target_compile_features(jazz_engine_api PUBLIC cxx_std_17)
