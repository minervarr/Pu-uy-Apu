/**
 * @file sleep_tracker_jni.cpp
 * @brief Fixed JNI bridge for Puñuy Apu sleep detection
 *
 * High-performance JNI interface with proper error handling and type conversions.
 * All C++ sleep detection processing exposed to Java layer.
 *
 * @author Puñuy Apu Development Team
 * @version 1.0 - Phase 1A: C++ Core (Fixed)
 */

#include <jni.h>
#include <android/log.h>
#include <memory>
#include <chrono>
#include <string>

// Include our fixed headers
#include "puuyapu_types.h"
#include "sleep_detector.h"

using namespace puuyapu;

// Global instances to avoid recreation overhead (thread-safe)
static std::unique_ptr<SleepDetector> g_sleepDetector;
static std::mutex g_detectorMutex;

// JNI class and method ID caching for performance
static jclass g_sleepResultClass = nullptr;
static jmethodID g_sleepResultConstructor = nullptr;
static jclass g_interruptionClass = nullptr;
static jmethodID g_interruptionConstructor = nullptr;

// Performance monitoring
static std::unordered_map<std::string, std::chrono::microseconds> g_jniMetrics;
static std::mutex g_metricsMutex;

constexpr const char* JNI_LOG_TAG = "PuuyApu_JNI";

/**
 * @brief Performance timing utility for JNI methods
 * RAII class for automatic performance measurement
 */
class JNIPerformanceTimer {
private:
    std::string operation_;
    std::chrono::high_resolution_clock::time_point startTime_;

public:
    explicit JNIPerformanceTimer(const std::string& operation)
            : operation_(operation), startTime_(std::chrono::high_resolution_clock::now()) {}

    ~JNIPerformanceTimer() {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                endTime - startTime_
        );

        std::lock_guard<std::mutex> lock(g_metricsMutex);
        g_jniMetrics[operation_] = duration;

#ifdef DEBUG
        __android_log_print(ANDROID_LOG_DEBUG, JNI_LOG_TAG,
            "JNI %s took %ld microseconds", operation_.c_str(), duration.count());
#endif
    }
};

/**
 * @brief Safe JNI array access with automatic cleanup (FIXED)
 * RAII wrapper that properly handles jboolean vs bool type conversion
 */
template<typename T>
class SafeJNIArray {
private:
    JNIEnv* env_;
    T* elements_;
    jarray array_;
    jboolean isCopy_;  // FIXED: Use jboolean instead of bool

public:
    SafeJNIArray(JNIEnv* env, jarray array) : env_(env), array_(array), isCopy_(JNI_FALSE) {
        if constexpr (std::is_same_v<T, jlong>) {
            elements_ = env_->GetLongArrayElements(static_cast<jlongArray>(array_), &isCopy_);
        } else if constexpr (std::is_same_v<T, jint>) {
            elements_ = env_->GetIntArrayElements(static_cast<jintArray>(array_), &isCopy_);
        }
    }

    ~SafeJNIArray() {
        if (elements_) {
            if constexpr (std::is_same_v<T, jlong>) {
                env_->ReleaseLongArrayElements(static_cast<jlongArray>(array_), elements_, JNI_ABORT);
            } else if constexpr (std::is_same_v<T, jint>) {
                env_->ReleaseIntArrayElements(static_cast<jintArray>(array_), elements_, JNI_ABORT);
            }
        }
    }

    T* get() const { return elements_; }
    bool isValid() const { return elements_ != nullptr; }
};

/**
 * @brief Initialize JNI class references and method IDs
 * Caches JNI references for performance
 */
bool initializeJNIReferences(JNIEnv* env) {
    // Cache SleepDetectionResult class and constructor
    jclass localSleepResultClass = env->FindClass("io/nava/puuyapu/app/models/SleepDetectionResult");
    if (!localSleepResultClass) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Failed to find SleepDetectionResult class");
        return false;
    }

    g_sleepResultClass = static_cast<jclass>(env->NewGlobalRef(localSleepResultClass));
    g_sleepResultConstructor = env->GetMethodID(g_sleepResultClass, "<init>",
                                                "(JJDILjava/util/List;DZ)V");

    if (!g_sleepResultConstructor) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Failed to find SleepDetectionResult constructor");
        return false;
    }

    // Cache SleepInterruption class and constructor
    jclass localInterruptionClass = env->FindClass("io/nava/puuyapu/app/models/SleepInterruption");
    if (!localInterruptionClass) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Failed to find SleepInterruption class");
        return false;
    }

    g_interruptionClass = static_cast<jclass>(env->NewGlobalRef(localInterruptionClass));
    g_interruptionConstructor = env->GetMethodID(g_interruptionClass, "<init>", "(JJI)V");

    env->DeleteLocalRef(localSleepResultClass);
    env->DeleteLocalRef(localInterruptionClass);

    return g_interruptionConstructor != nullptr;
}

/**
 * @brief Create Java SleepDetectionResult object from C++ result (FIXED)
 * Handles proper type conversions and null cases
 */
jobject createJavaSleepResult(JNIEnv* env, const SleepDetectionResult& result) {
    if (!g_sleepResultClass || !g_sleepResultConstructor) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "JNI references not initialized");
        return nullptr;
    }

    // Convert time points to milliseconds since epoch (Java long)
    jlong bedtimeMs = 0;
    jlong wakeTimeMs = 0;

    if (result.bedtime.has_value()) {
        auto bedtimeEpoch = result.bedtime.value().time_since_epoch();
        bedtimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(bedtimeEpoch).count();
    }

    if (result.wake_time.has_value()) {
        auto wakeTimeEpoch = result.wake_time.value().time_since_epoch();
        wakeTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(wakeTimeEpoch).count();
    }

    // Create Java ArrayList for interruptions
    jclass arrayListClass = env->FindClass("java/util/ArrayList");
    jmethodID arrayListConstructor = env->GetMethodID(arrayListClass, "<init>", "()V");
    jmethodID arrayListAdd = env->GetMethodID(arrayListClass, "add", "(Ljava/lang/Object;)Z");

    jobject interruptionsList = env->NewObject(arrayListClass, arrayListConstructor);

    // Add interruptions to list
    for (const auto& interruption : result.interruptions) {
        auto interruptionEpoch = interruption.timestamp.time_since_epoch();
        jlong interruptionMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                interruptionEpoch).count();

        jlong durationMs = interruption.duration.count();
        jint interactionType = static_cast<jint>(interruption.cause);

        jobject javaInterruption = env->NewObject(g_interruptionClass, g_interruptionConstructor,
                                                  interruptionMs, durationMs, interactionType);

        if (javaInterruption) {
            env->CallBooleanMethod(interruptionsList, arrayListAdd, javaInterruption);
            env->DeleteLocalRef(javaInterruption);
        }
    }

    // Create main result object
    jdouble durationHours = result.duration.count();
    jint confidence = static_cast<jint>(result.confidence);
    jdouble qualityScore = result.quality_score;
    jboolean isManuallyConfirmed = result.is_manually_confirmed;

    jobject javaSleepResult = env->NewObject(g_sleepResultClass, g_sleepResultConstructor,
                                             bedtimeMs, wakeTimeMs, durationHours, confidence,
                                             interruptionsList, qualityScore, isManuallyConfirmed);

    // Clean up local references
    env->DeleteLocalRef(arrayListClass);
    env->DeleteLocalRef(interruptionsList);

    return javaSleepResult;
}

// ============================================================================
// JNI Method Implementations
// ============================================================================

extern "C" JNIEXPORT jboolean JNICALL
Java_io_nava_puuyapu_app_native_NativeSleepTracker_initializeNative(
        JNIEnv* env, jobject thiz) {

    JNIPerformanceTimer timer("initializeNative");

    std::lock_guard<std::mutex> lock(g_detectorMutex);

    try {
        // Initialize JNI references first
        if (!initializeJNIReferences(env)) {
            __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                                "Failed to initialize JNI references");
            return JNI_FALSE;
        }

        // Create sleep detector with default preferences
        UserPreferences defaultPrefs;
        g_sleepDetector = std::make_unique<SleepDetector>(defaultPrefs);

        __android_log_print(ANDROID_LOG_INFO, JNI_LOG_TAG,
                            "Native sleep detector initialized successfully");

        return JNI_TRUE;

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Exception during initialization: %s", e.what());
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nava_puuyapu_app_native_NativeSleepTracker_addInteractionEvent(
        JNIEnv* env, jobject thiz,
        jlong timestamp, jint appType, jlong duration) {

    JNIPerformanceTimer timer("addInteractionEvent");

    if (!g_sleepDetector) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Sleep detector not initialized");
        return static_cast<jint>(InteractionType::UNKNOWN);
    }

    try {
        // Create interaction event from Java parameters
        InteractionEvent event;
        event.timestamp = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(timestamp)
        );
        event.duration = std::chrono::milliseconds(duration);
        event.category = static_cast<AppCategory>(appType);

        // Classify interaction type based on duration and context
        if (duration < 30000) { // Less than 30 seconds
            event.type = InteractionType::TIME_CHECK;
        } else if (duration < 300000) { // Less than 5 minutes
            event.type = InteractionType::MEANINGFUL_USE;
        } else {
            event.type = InteractionType::EXTENDED_USE;
        }

        // Add event to detector for processing
        g_sleepDetector->addInteractionEvent(event);

        return static_cast<jint>(event.type);

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Exception in addInteractionEvent: %s", e.what());
        return static_cast<jint>(InteractionType::UNKNOWN);
    }
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_nava_puuyapu_app_native_NativeSleepTracker_detectSleep(
        JNIEnv* env, jobject thiz) {

    JNIPerformanceTimer timer("detectSleep");

    if (!g_sleepDetector) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Sleep detector not initialized");
        return nullptr;
    }

    try {
        // Perform sleep detection
        auto currentTime = std::chrono::system_clock::now();
        auto result = g_sleepDetector->detectSleepPeriod(currentTime);

        // Convert C++ result to Java object
        return createJavaSleepResult(env, result);

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Exception in detectSleep: %s", e.what());
        return nullptr;
    }
}

extern "C" JNIEXPORT jdouble JNICALL
Java_io_nava_puuyapu_app_native_NativeSleepTracker_calculateConfidence(
        JNIEnv* env, jobject thiz,
        jlong bedtime, jlong wakeTime) {

    JNIPerformanceTimer timer("calculateConfidence");

    if (!g_sleepDetector) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Sleep detector not initialized");
        return 0.0;
    }

    try {
        // Create sleep result for confidence calculation
        SleepDetectionResult session;
        session.bedtime = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(bedtime)
        );
        session.wake_time = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(wakeTime)
        );

        // Calculate duration
        if (session.bedtime.has_value() && session.wake_time.has_value()) {
            session.duration = std::chrono::duration<double, std::ratio<3600>>(
                    calculateDurationHours(session.bedtime.value(), session.wake_time.value())
            );
        }

        double confidence = g_sleepDetector->calculateConfidenceScore(session);
        return static_cast<jdouble>(confidence);

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Exception in calculateConfidence: %s", e.what());
        return 0.0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_app_native_NativeSleepTracker_updateUserPreferences(
        JNIEnv* env, jobject thiz,
        jdouble targetSleepHours, jlong preferredBedtime, jlong preferredWakeTime) {

    JNIPerformanceTimer timer("updateUserPreferences");

    if (!g_sleepDetector) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Sleep detector not initialized");
        return;
    }

    try {
        UserPreferences preferences;
        preferences.target_sleep_hours = std::chrono::duration<double, std::ratio<3600>>(targetSleepHours);

        // Convert timestamps to minutes since midnight
        auto bedtime_tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(preferredBedtime));
        auto waketime_tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(preferredWakeTime));

        preferences.target_bedtime = getMinutesSinceMidnight(bedtime_tp);
        preferences.target_wake_time = getMinutesSinceMidnight(waketime_tp);
        preferences.weekday_bedtime = preferences.target_bedtime;
        preferences.weekend_bedtime = preferences.target_bedtime;

        g_sleepDetector->updateUserPreferences(preferences);

        __android_log_print(ANDROID_LOG_INFO, JNI_LOG_TAG,
                            "User preferences updated: target=%.1f hours", targetSleepHours);

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Exception in updateUserPreferences: %s", e.what());
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_nava_puuyapu_app_native_NativeSleepTracker_isCurrentlyAsleep(
        JNIEnv* env, jobject thiz) {

    JNIPerformanceTimer timer("isCurrentlyAsleep");

    if (!g_sleepDetector) {
        return JNI_FALSE;
    }

    try {
        auto currentTime = std::chrono::system_clock::now();
        bool isAsleep = g_sleepDetector->isCurrentlyAsleep(currentTime);
        return isAsleep ? JNI_TRUE : JNI_FALSE;

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Exception in isCurrentlyAsleep: %s", e.what());
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_io_nava_puuyapu_app_native_NativeSleepTracker_getEstimatedSleepStart(
        JNIEnv* env, jobject thiz) {

    JNIPerformanceTimer timer("getEstimatedSleepStart");

    if (!g_sleepDetector) {
        return 0;
    }

    try {
        auto currentTime = std::chrono::system_clock::now();
        auto sleepStart = g_sleepDetector->getEstimatedSleepStart(currentTime);

        if (sleepStart.has_value()) {
            auto epoch = sleepStart.value().time_since_epoch();
            return std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
        }

        return 0;

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Exception in getEstimatedSleepStart: %s", e.what());
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_app_native_NativeSleepTracker_clearOldData(
        JNIEnv* env, jobject thiz, jlong cutoffTimestamp) {

    JNIPerformanceTimer timer("clearOldData");

    if (!g_sleepDetector) {
        return;
    }

    try {
        auto cutoffTime = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(cutoffTimestamp)
        );

        g_sleepDetector->clearOldData(cutoffTime);

        __android_log_print(ANDROID_LOG_DEBUG, JNI_LOG_TAG,
                            "Cleared data older than timestamp %lld", cutoffTimestamp);

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Exception in clearOldData: %s", e.what());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_app_native_NativeSleepTracker_optimizeMemory(
        JNIEnv* env, jobject thiz) {

    JNIPerformanceTimer timer("optimizeMemory");

    try {
        if (g_sleepDetector) {
            g_sleepDetector->optimizeMemory();
        }

        // Clear JNI performance metrics
        {
            std::lock_guard<std::mutex> lock(g_metricsMutex);
            g_jniMetrics.clear();
        }

        __android_log_print(ANDROID_LOG_INFO, JNI_LOG_TAG, "Memory optimization completed");

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Exception in optimizeMemory: %s", e.what());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_app_native_NativeSleepTracker_confirmManualSleep(
        JNIEnv* env, jobject thiz, jlong timestamp) {

    JNIPerformanceTimer timer("confirmManualSleep");

    if (!g_sleepDetector) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Sleep detector not initialized");
        return;
    }

    try {
        auto sleepTime = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(timestamp)
        );

        g_sleepDetector->confirmManualSleep(sleepTime);

        __android_log_print(ANDROID_LOG_INFO, JNI_LOG_TAG,
                            "Manual sleep confirmation recorded at timestamp %lld", timestamp);

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Exception in confirmManualSleep: %s", e.what());
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_nava_puuyapu_app_native_NativeSleepTracker_getPerformanceMetrics(
        JNIEnv* env, jobject thiz) {

    JNIPerformanceTimer timer("getPerformanceMetrics");

    try {
        // Create JSON string with performance metrics
        std::string json = "{";

        if (g_sleepDetector) {
            auto metrics = g_sleepDetector->getPerformanceMetrics();
            bool first = true;

            for (const auto& [operation, duration] : metrics) {
                if (!first) json += ",";
                json += "\"" + operation + "\":" + std::to_string(duration.count());
                first = false;
            }

            // Add JNI metrics
            std::lock_guard<std::mutex> lock(g_metricsMutex);
            for (const auto& [operation, duration] : g_jniMetrics) {
                if (!first) json += ",";
                json += "\"jni_" + operation + "\":" + std::to_string(duration.count());
                first = false;
            }
        }

        json += "}";

        return env->NewStringUTF(json.c_str());

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG,
                            "Exception in getPerformanceMetrics: %s", e.what());
        return env->NewStringUTF("{}");
    }
}

// Library lifecycle management

extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* reserved) {
    __android_log_print(ANDROID_LOG_INFO, JNI_LOG_TAG,
                        "Native library loaded successfully");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
JNI_OnUnload(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        // Clean up global references
        if (g_sleepResultClass) {
            env->DeleteGlobalRef(g_sleepResultClass);
            g_sleepResultClass = nullptr;
        }
        if (g_interruptionClass) {
            env->DeleteGlobalRef(g_interruptionClass);
            g_interruptionClass = nullptr;
        }
    }

    // Clean up C++ objects
    {
        std::lock_guard<std::mutex> lock(g_detectorMutex);
        g_sleepDetector.reset();
    }

    __android_log_print(ANDROID_LOG_INFO, JNI_LOG_TAG,
                        "Native library unloaded successfully");
}