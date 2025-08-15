// sleep_tracker_jni.cpp - Optimized JNI interface
#include <jni.h>
#include "sleep_detector.h"
#include <memory>

// Global instances for performance (avoid recreation)
static std::unique_ptr<SleepDetector> g_sleepDetector;
static std::unique_ptr<InteractionAnalyzer> g_interactionAnalyzer;

extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_initializeNative(
        JNIEnv* env, jobject thiz
) {
    // Initialize C++ components once
    g_sleepDetector = std::make_unique<SleepDetector>();
    g_interactionAnalyzer = std::make_unique<InteractionAnalyzer>();
}

extern "C" JNIEXPORT jlong JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_addInteractionEvent(
        JNIEnv* env, jobject thiz,
        jlong timestamp, jint appType, jlong duration
) {
    // Ultra-fast event processing
    InteractionEvent event{
            .timestamp = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(timestamp)
            ),
            .appType = static_cast<AppType>(appType),
            .duration = std::chrono::milliseconds(duration)
    };

    // Process immediately for real-time analysis
    auto type = g_interactionAnalyzer->classifyInteraction(event, {});

    return static_cast<jlong>(type);
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_detectSleep(
        JNIEnv* env, jobject thiz,
        jlongArray eventTimestamps, jintArray eventTypes
) {
    // Convert Java arrays to C++ vectors (optimized)
    jsize length = env->GetArrayLength(eventTimestamps);
    jlong* timestamps = env->GetLongArrayElements(eventTimestamps, nullptr);
    jint* types = env->GetIntArrayElements(eventTypes, nullptr);

    std::vector<InteractionEvent> events;
    events.reserve(length); // Pre-allocate for performance

    for (int i = 0; i < length; i++) {
        events.emplace_back(InteractionEvent{
                .timestamp = std::chrono::system_clock::time_point(
                        std::chrono::milliseconds(timestamps[i])
                ),
                .type = static_cast<InteractionType>(types[i])
        });
    }

    // Fast sleep detection
    auto result = g_sleepDetector->detectSleepPeriod(
            events, std::chrono::system_clock::now()
    );

    // Release JNI arrays
    env->ReleaseLongArrayElements(eventTimestamps, timestamps, JNI_ABORT);
    env->ReleaseIntArrayElements(eventTypes, types, JNI_ABORT);

    // Convert result to Java object (create helper method)
    return createJavaSleepResult(env, result);
}