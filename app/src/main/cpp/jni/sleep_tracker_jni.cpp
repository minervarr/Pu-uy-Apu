// jni/sleep_tracker_jni.cpp - Complete optimized JNI interface
#include <jni.h>
#include <string>
#include <memory>
#include <vector>
#include <android/log.h>
#include "../core/sleep_detector.cpp"  // Include full implementation

#define LOG_TAG "PuuyApu_JNI"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace puuyapu;

// Global instances for performance (avoid recreation)
static std::unique_ptr<SleepDetector> g_sleepDetector;
static std::vector<InteractionEvent> g_eventHistory;
static std::mutex g_mutex; // Thread safety for background processing

// Helper function to create Java SleepDetectionResult object
jobject createJavaSleepResult(JNIEnv* env, const SleepDetectionResult& result) {
    // Find the SleepDetectionResult class
    jclass resultClass = env->FindClass("io/nava/puuyapu/models/SleepDetectionResult");
    if (!resultClass) {
        LOGE("Could not find SleepDetectionResult class");
        return nullptr;
    }

    // Get constructor
    jmethodID constructor = env->GetMethodID(resultClass, "<init>", "()V");
    if (!constructor) {
        LOGE("Could not find SleepDetectionResult constructor");
        return nullptr;
    }

    // Create new instance
    jobject resultObj = env->NewObject(resultClass, constructor);
    if (!resultObj) {
        LOGE("Could not create SleepDetectionResult instance");
        return nullptr;
    }

    // Set fields
    jfieldID sleepDetectedField = env->GetFieldID(resultClass, "sleepDetected", "Z");
    jfieldID bedtimeField = env->GetFieldID(resultClass, "bedtime", "J");
    jfieldID wakeTimeField = env->GetFieldID(resultClass, "wakeTime", "J");
    jfieldID durationField = env->GetFieldID(resultClass, "duration", "J");
    jfieldID confidenceField = env->GetFieldID(resultClass, "confidence", "I");
    jfieldID qualityScoreField = env->GetFieldID(resultClass, "qualityScore", "D");
    jfieldID isOngoingField = env->GetFieldID(resultClass, "isOngoing", "Z");

    if (sleepDetectedField && bedtimeField && wakeTimeField && durationField &&
        confidenceField && qualityScoreField && isOngoingField) {

        env->SetBooleanField(resultObj, sleepDetectedField, result.sleep_detected);
        env->SetLongField(resultObj, bedtimeField,
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                                  result.bedtime.time_since_epoch()).count());
        env->SetLongField(resultObj, wakeTimeField,
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                                  result.wake_time.time_since_epoch()).count());
        env->SetLongField(resultObj, durationField, result.duration.count());
        env->SetIntField(resultObj, confidenceField, static_cast<int>(result.confidence));
        env->SetDoubleField(resultObj, qualityScoreField, result.quality_score);
        env->SetBooleanField(resultObj, isOngoingField, result.is_ongoing);
    }

    return resultObj;
}

// Helper function to convert Java arrays to C++ vectors efficiently
std::vector<InteractionEvent> convertJavaEvents(JNIEnv* env,
                                                jlongArray timestamps,
                                                jintArray types,
                                                jlongArray durations) {
    std::vector<InteractionEvent> events;

    jsize length = env->GetArrayLength(timestamps);
    if (length <= 0) return events;

    events.reserve(length);

    // Get array elements
    jlong* timestampArray = env->GetLongArrayElements(timestamps, nullptr);
    jint* typeArray = env->GetIntArrayElements(types, nullptr);
    jlong* durationArray = env->GetLongArrayElements(durations, nullptr);

    if (timestampArray && typeArray && durationArray) {
        for (jsize i = 0; i < length; i++) {
            InteractionEvent event(
                    std::chrono::system_clock::time_point(
                            std::chrono::milliseconds(timestampArray[i])
                    ),
                    std::chrono::milliseconds(durationArray[i]),
                    static_cast<InteractionType>(typeArray[i])
            );
            events.push_back(event);
        }
    }

    // Release arrays
    if (timestampArray) env->ReleaseLongArrayElements(timestamps, timestampArray, JNI_ABORT);
    if (typeArray) env->ReleaseIntArrayElements(types, typeArray, JNI_ABORT);
    if (durationArray) env->ReleaseLongArrayElements(durations, durationArray, JNI_ABORT);

    return events;
}

/**
 * Initialize the native sleep detection engine
 * Called once when NativeSleepTracker is first loaded
 */
extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_initializeNative(
        JNIEnv* env, jobject thiz) {

    std::lock_guard<std::mutex> lock(g_mutex);

    try {
        // Initialize with default preferences
        UserPreferences defaultPrefs;
        g_sleepDetector = std::make_unique<SleepDetector>(defaultPrefs);

        // Reserve space for event history
        g_eventHistory.reserve(10000); // Store up to 10k events

        LOGD("Native sleep detector initialized successfully");

    } catch (const std::exception& e) {
        LOGE("Failed to initialize native sleep detector: %s", e.what());
    }
}

/**
 * Add a single interaction event for real-time processing
 * Optimized for frequent calls from background service
 */
extern "C" JNIEXPORT jlong JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_addInteractionEvent(
        JNIEnv* env, jobject thiz,
        jlong timestamp, jint appType, jlong duration) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_sleepDetector) {
        LOGE("Sleep detector not initialized");
        return 0;
    }

    try {
        // Create interaction event
        InteractionEvent event(
                std::chrono::system_clock::time_point(std::chrono::milliseconds(timestamp)),
                std::chrono::milliseconds(duration),
                static_cast<InteractionType>(appType)
        );

        // Add to history
        g_eventHistory.push_back(event);

        // Maintain history size for performance
        if (g_eventHistory.size() > 5000) {
            // Remove old events (keep last 3000)
            g_eventHistory.erase(g_eventHistory.begin(),
                                 g_eventHistory.begin() + 2000);
        }

        // Return event timestamp for confirmation
        return timestamp;

    } catch (const std::exception& e) {
        LOGE("Error adding interaction event: %s", e.what());
        return 0;
    }
}

/**
 * Batch process multiple interaction events
 * More efficient than individual calls for bulk operations
 */
extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_processBatchInteractions(
        JNIEnv* env, jobject thiz,
        jlongArray timestamps, jintArray types, jlongArray durations) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_sleepDetector) {
        LOGE("Sleep detector not initialized");
        return;
    }

    try {
        auto events = convertJavaEvents(env, timestamps, types, durations);

        // Add all events to history
        g_eventHistory.insert(g_eventHistory.end(), events.begin(), events.end());

        // Sort by timestamp for efficient processing
        std::sort(g_eventHistory.begin(), g_eventHistory.end());

        LOGD("Processed batch of %zu events", events.size());

    } catch (const std::exception& e) {
        LOGE("Error processing batch interactions: %s", e.what());
    }
}

/**
 * Main sleep detection function
 * Analyzes all events and returns comprehensive sleep data
 */
extern "C" JNIEXPORT jobject JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_detectSleep(
        JNIEnv* env, jobject thiz,
        jlongArray eventTimestamps, jintArray eventTypes, jlongArray eventDurations) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_sleepDetector) {
        LOGE("Sleep detector not initialized");
        return nullptr;
    }

    try {
        std::vector<InteractionEvent> events;

        // Use provided events if available, otherwise use history
        if (eventTimestamps && eventTypes && eventDurations) {
            events = convertJavaEvents(env, eventTimestamps, eventTypes, eventDurations);
        } else {
            events = g_eventHistory;
        }

        if (events.empty()) {
            LOGD("No events available for sleep detection");
            return nullptr;
        }

        // Perform sleep detection
        auto current_time = std::chrono::system_clock::now();
        auto result = g_sleepDetector->detectSleepPeriod(events, current_time);

        // Convert to Java object
        return createJavaSleepResult(env, result);

    } catch (const std::exception& e) {
        LOGE("Error in sleep detection: %s", e.what());
        return nullptr;
    }
}

/**
 * Calculate confidence score for a specific sleep period
 * Used for validation and quality assessment
 */
extern "C" JNIEXPORT jdouble JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_calculateConfidence(
        JNIEnv* env, jobject thiz,
        jlong bedtime, jlong wakeTime) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_sleepDetector) {
        LOGE("Sleep detector not initialized");
        return 0.0;
    }

    try {
        // Create time gap for analysis
        auto bedtime_tp = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(bedtime));
        auto waketime_tp = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(wakeTime));

        TimeGap gap(bedtime_tp, waketime_tp);

        // Use current event history for context
        auto current_time = std::chrono::system_clock::now();
        auto result = g_sleepDetector->detectSleepPeriod(g_eventHistory, current_time);

        // Return confidence as double
        return static_cast<double>(result.confidence) / 4.0; // Normalize to 0-1

    } catch (const std::exception& e) {
        LOGE("Error calculating confidence: %s", e.what());
        return 0.0;
    }
}

/**
 * Update user preferences for personalized detection
 * Allows real-time adjustment of detection parameters
 */
extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_updateUserPreferences(
        JNIEnv* env, jobject thiz,
        jdouble targetSleepHours, jlong preferredBedtime, jlong preferredWakeTime) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_sleepDetector) {
        LOGE("Sleep detector not initialized");
        return;
    }

    try {
        UserPreferences prefs;
        prefs.target_sleep_duration = std::chrono::minutes(
                static_cast<long>(targetSleepHours * 60));
        prefs.target_bedtime = std::chrono::minutes(preferredBedtime);
        prefs.target_wake_time = std::chrono::minutes(preferredWakeTime);

        g_sleepDetector->updatePreferences(prefs);

        LOGD("Updated preferences: target sleep %.1f hours", targetSleepHours);

    } catch (const std::exception& e) {
        LOGE("Error updating preferences: %s", e.what());
    }
}

/**
 * Real-time check if user is currently asleep
 * Optimized for frequent polling without heavy computation
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_isCurrentlyAsleep(
        JNIEnv* env, jobject thiz) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_sleepDetector) {
        return JNI_FALSE;
    }

    try {
        auto current_time = std::chrono::system_clock::now();
        bool is_asleep = g_sleepDetector->isCurrentlyAsleep(g_eventHistory, current_time);

        return is_asleep ? JNI_TRUE : JNI_FALSE;

    } catch (const std::exception& e) {
        LOGE("Error checking current sleep status: %s", e.what());
        return JNI_FALSE;
    }
}

/**
 * Get estimated sleep start time for ongoing sleep
 * Returns 0 if not currently asleep
 */
extern "C" JNIEXPORT jlong JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_getEstimatedSleepStart(
        JNIEnv* env, jobject thiz) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_sleepDetector) {
        return 0;
    }

    try {
        auto current_time = std::chrono::system_clock::now();
        auto result = g_sleepDetector->detectSleepPeriod(g_eventHistory, current_time);

        if (result.is_ongoing && result.sleep_detected) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                    result.bedtime.time_since_epoch()).count();
        }

        return 0;

    } catch (const std::exception& e) {
        LOGE("Error getting sleep start time: %s", e.what());
        return 0;
    }
}

/**
 * Clear old interaction data to free memory
 * Called periodically for memory management
 */
extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_clearOldData(
        JNIEnv* env, jobject thiz, jlong cutoffTimestamp) {

    std::lock_guard<std::mutex> lock(g_mutex);

    try {
        auto cutoff_time = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(cutoffTimestamp));

        // Remove events older than cutoff
        g_eventHistory.erase(
                std::remove_if(g_eventHistory.begin(), g_eventHistory.end(),
                               [cutoff_time](const InteractionEvent& event) {
                                   return event.timestamp < cutoff_time;
                               }),
                g_eventHistory.end());

        LOGD("Cleared old data, %zu events remaining", g_eventHistory.size());

    } catch (const std::exception& e) {
        LOGE("Error clearing old data: %s", e.what());
    }
}

/**
 * Optimize memory usage and performance
 * Defragments internal data structures
 */
extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_optimizeMemory(
        JNIEnv* env, jobject thiz) {

    std::lock_guard<std::mutex> lock(g_mutex);

    try {
        // Sort events by timestamp for better cache performance
        std::sort(g_eventHistory.begin(), g_eventHistory.end());

        // Shrink vector to fit current size
        g_eventHistory.shrink_to_fit();

        LOGD("Memory optimization complete, %zu events in history",
             g_eventHistory.size());

    } catch (const std::exception& e) {
        LOGE("Error optimizing memory: %s", e.what());
    }
}

/**
 * Get performance metrics for monitoring and debugging
 * Returns JSON string with performance data
 */
extern "C" JNIEXPORT jstring JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_getPerformanceMetrics(
        JNIEnv* env, jobject thiz) {

    std::lock_guard<std::mutex> lock(g_mutex);

    try {
        // Create performance metrics JSON
        std::string metrics = "{";
        metrics += "\"event_count\":" + std::to_string(g_eventHistory.size()) + ",";
        metrics += "\"memory_usage_kb\":" + std::to_string(
                g_eventHistory.size() * sizeof(InteractionEvent) / 1024) + ",";
        metrics += "\"detector_initialized\":" +
                   (g_sleepDetector ? "true" : "false");
        metrics += "}";

        return env->NewStringUTF(metrics.c_str());

    } catch (const std::exception& e) {
        LOGE("Error getting performance metrics: %s", e.what());
        return env->NewStringUTF("{}");
    }
}

/**
 * Export sleep data as JSON for backup/analysis
 * Efficient serialization of all sleep sessions
 */
extern "C" JNIEXPORT jstring JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_exportSleepDataAsJson(
        JNIEnv* env, jobject thiz, jlong startTimestamp, jlong endTimestamp) {

    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_sleepDetector) {
        return env->NewStringUTF("{}");
    }

    try {
        auto start_time = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(startTimestamp));
        auto end_time = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(endTimestamp));

        // Filter events within time range
        std::vector<InteractionEvent> filtered_events;
        std::copy_if(g_eventHistory.begin(), g_eventHistory.end(),
                     std::back_inserter(filtered_events),
                     [start_time, end_time](const InteractionEvent& event) {
                         return event.timestamp >= start_time &&
                                event.timestamp <= end_time;
                     });

        // Analyze filtered events for sleep periods
        auto current_time = std::chrono::system_clock::now();
        auto result = g_sleepDetector->detectSleepPeriod(filtered_events, current_time);

        // Create JSON export
        std::string json = "{";
        json += "\"export_timestamp\":" + std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                        current_time.time_since_epoch()).count()) + ",";
        json += "\"start_time\":" + std::to_string(startTimestamp) + ",";
        json += "\"end_time\":" + std::to_string(endTimestamp) + ",";
        json += "\"sleep_detected\":" + (result.sleep_detected ? "true" : "false") + ",";

        if (result.sleep_detected) {
            json += "\"bedtime\":" + std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                            result.bedtime.time_since_epoch()).count()) + ",";
            json += "\"wake_time\":" + std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                            result.wake_time.time_since_epoch()).count()) + ",";
            json += "\"duration_minutes\":" + std::to_string(result.duration.count()) + ",";
            json += "\"confidence\":" + std::to_string(static_cast<int>(result.confidence)) + ",";
            json += "\"quality_score\":" + std::to_string(result.quality_score) + ",";
            json += "\"interruption_count\":" + std::to_string(result.interruptions.size());
        }

        json += "}";

        return env->NewStringUTF(json.c_str());

    } catch (const std::exception& e) {
        LOGE("Error exporting sleep data: %s", e.what());
        return env->NewStringUTF("{}");
    }
}

/**
 * JNI cleanup function
 * Called when library is unloaded
 */
extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_cleanup(
        JNIEnv* env, jobject thiz) {

    std::lock_guard<std::mutex> lock(g_mutex);

    try {
        g_sleepDetector.reset();
        g_eventHistory.clear();
        g_eventHistory.shrink_to_fit();

        LOGD("Native sleep tracker cleanup complete");

    } catch (const std::exception& e) {
        LOGE("Error during cleanup: %s", e.what());
    }
}