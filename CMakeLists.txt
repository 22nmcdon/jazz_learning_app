cmake_minimum_required(VERSION 3.22)

project(JazzLearningApp
        VERSION 0.1.0
        DESCRIPTION "Jazz learning app - reharmonisation assistant and voicing analyser"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)              # JUCE 8 requires C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(JAZZ_BUILD_APP   "Build the JUCE standalone application (requires JUCE)" ON)
option(JAZZ_BUILD_TESTS "Build the core engine unit tests" ON)

# The core engine is plain C++ with no JUCE dependency, so it always builds -
# including on a machine that has no JUCE and no GUI toolkit installed. That is
# the architectural boundary the design doc asks for, enforced by the build.
add_subdirectory(modules/core_engine)

if(JAZZ_BUILD_APP)
    include(cmake/GetJUCE.cmake)
    add_subdirectory(modules/shared_ui)
    add_subdirectory(app)
endif()

if(JAZZ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "Jazz Learning App ${PROJECT_VERSION}")
message(STATUS "  Standalone app : ${JAZZ_BUILD_APP}")
message(STATUS "  Engine tests   : ${JAZZ_BUILD_TESTS}")
