#!/bin/sh

# Create the root directory structure
mkdir -p app/src/main/cpp/{include,core,jni,models}

# Create all header files in include/
touch app/src/main/cpp/include/{sleep_detector.h,interaction_analyzer.h,pattern_matcher.h,time_utils.h,data_processor.h}

# Create core implementation files
touch app/src/main/cpp/core/{sleep_detector.cpp,interaction_analyzer.cpp,pattern_matcher.cpp,time_utils.cpp,data_processor.cpp}

# Create JNI bridge files
touch app/src/main/cpp/jni/{sleep_tracker_jni.cpp,data_bridge.cpp}

# Create model implementation files
touch app/src/main/cpp/models/{sleep_session.cpp,interaction_event.cpp,user_preferences.cpp}

# Print confirmation
echo "Directory structure created successfully:"
tree app/src/main/cpp/
