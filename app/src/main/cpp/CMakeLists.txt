cmake_minimum_required(VERSION 3.22.1)
project("puuyapu")

# Aggressive optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -flto")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

# Enable C++17 for performance features
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags for different architectures
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a")
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv7-a -mfpu=neon")
endif()

# Source files
file(GLOB_RECURSE SOURCES
        "core/*.cpp"
        "jni/*.cpp"
        "models/*.cpp"
)

# Create the library
add_library(${CMAKE_PROJECT_NAME} SHARED ${SOURCES})

# Link libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        log
        # Add any additional libraries for ML or math operations
)

# Include directories
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE include/)