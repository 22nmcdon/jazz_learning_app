# Locates JUCE, in this order:
#
#   1. -DJUCE_PATH=/path/to/JUCE          (a checkout you already have)
#   2. a JUCE/ directory beside this repo
#   3. FetchContent from GitHub           (needs network on first configure)
#
# Pinning the version here rather than relying on a system install keeps CI and
# every developer machine on the same JUCE.

set(JAZZ_JUCE_VERSION "8.0.4" CACHE STRING "JUCE version to fetch when no local copy is given")
set(JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout")

if(JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(STATUS "Using JUCE from JUCE_PATH: ${JUCE_PATH}")
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
    message(STATUS "Using JUCE checkout beside the repository")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    message(STATUS "Fetching JUCE ${JAZZ_JUCE_VERSION} with FetchContent")

    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${JAZZ_JUCE_VERSION}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()
