cmake_minimum_required(VERSION 3.22.1)
project("puuyapu")

# ============================================================================
# PUÑUY APU - HIGH-PERFORMANCE SLEEP TRACKER
# Phase 1A: C++ Core - Optimized CMake Configuration
# ============================================================================

# Aggressive optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -flto -ffast-math")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG")

# Enable C++17 for modern performance features
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Architecture-specific optimizations
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a -mtune=cortex-a53")
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv7-a -mfpu=neon -mtune=cortex-a9")
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=x86-64 -mtune=generic")
elseif(ANDROID_ABI STREQUAL "x86")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=i686 -mtune=generic")
endif()

# Additional performance flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")

# Include directories
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ============================================================================
# SOURCE FILES - Only include files that exist
# ============================================================================

# Core implementation files (these we will create)
set(CORE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/core/sleep_detector.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/models/interaction_event.cpp
)

# JNI bridge files
set(JNI_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/jni/sleep_tracker_jni.cpp
)

# Combine all sources
set(ALL_SOURCES ${CORE_SOURCES} ${JNI_SOURCES})

# ============================================================================
# CREATE THE LIBRARY
# ============================================================================

add_library(${CMAKE_PROJECT_NAME} SHARED ${ALL_SOURCES})

# ============================================================================
# LINK LIBRARIES
# ============================================================================

target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        log
        # Add additional libraries as needed for future ML features
)

# ============================================================================
# COMPILER DEFINITIONS
# ============================================================================

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        -DANDROID
        -D__ANDROID_API__=${ANDROID_NATIVE_API_LEVEL}
)

# Debug-specific definitions
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE -DDEBUG)
endif()

# ============================================================================
# LINKER OPTIMIZATIONS
# ============================================================================

# Strip unused sections in release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
            LINK_FLAGS "-Wl,--gc-sections -Wl,--strip-all"
    )
endif()

# ============================================================================
# OUTPUT CONFIGURATION
# ============================================================================

# Set output name
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
        OUTPUT_NAME "puuyapu"
)

# ============================================================================
# PERFORMANCE VALIDATION
# ============================================================================

# Print configuration info
message(STATUS "=== Puñuy Apu C++ Core Configuration ===")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Android ABI: ${ANDROID_ABI}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "C++ Flags: ${CMAKE_CXX_FLAGS}")
message(STATUS "Sources: ${ALL_SOURCES}")
message(STATUS "==========================================")

# Validate that critical source files exist (we'll create them)
foreach(SOURCE_FILE ${ALL_SOURCES})
    if(NOT EXISTS ${SOURCE_FILE})
        message(WARNING "Source file does not exist: ${SOURCE_FILE}")
    endif()
endforeach()