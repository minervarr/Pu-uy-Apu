// sleep_tracker_jni.cpp - High-performance JNI bridge with error handling
#include <jni.h>
#include <android/log.h>
#include <memory>
#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include "sleep_detector.h"

// Performance optimization: Global instances to avoid recreation overhead
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

/**
 * @brief Performance timing utility for JNI methods
 *
 * RAII class that automatically measures and records execution time
 * for JNI method calls to monitor performance in production.
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
        __android_log_print(ANDROID_LOG_DEBUG, "PuuyApu_JNI",
            "JNI %s took %ld microseconds", operation_.c_str(), duration.count());
#endif
    }
};

/**
 * @brief Safe JNI array access with automatic cleanup
 *
 * RAII wrapper for JNI array operations that ensures proper release
 * and handles error conditions gracefully.
 */
template<typename T>
class SafeJNIArray {
private:
    JNIEnv* env_;
    T* elements_;
    jarray array_;
    bool isCopy_;

public:
    SafeJNIArray(JNIEnv* env, jarray array) : env_(env), array_(array), isCopy_(false) {
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
 *
 * Caches JNI class references and method IDs for performance.
 * Called once during library initialization.
 *
 * @param env JNI environment
 * @return true if initialization successful
 */
bool initializeJNIReferences(JNIEnv* env) {
    // Cache SleepDetectionResult class and constructor
    jclass localSleepResultClass = env->FindClass("io/nava/puuyapu/app/models/SleepDetectionResult");
    if (!localSleepResultClass) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Failed to find SleepDetectionResult class");
        return false;
    }

    g_sleepResultClass = static_cast<jclass>(env->NewGlobalRef(localSleepResultClass));
    g_sleepResultConstructor = env->GetMethodID(g_sleepResultClass, "<init>",
                                                "(JJDILjava/util/List;DZ)V");

    if (!g_sleepResultConstructor) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Failed to find SleepDetectionResult constructor");
        return false;
    }

    // Cache SleepInterruption class and constructor
    jclass localInterruptionClass = env->FindClass("io/nava/puuyapu/app/models/SleepInterruption");
    if (!localInterruptionClass) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
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
 * @brief Create Java SleepDetectionResult object from C++ result
 *
 * Converts C++ SleepDetectionResult to Java object for JNI return.
 * Handles null cases and creates proper Java collections.
 *
 * @param env JNI environment
 * @param result C++ detection result
 * @return Java SleepDetectionResult object or null on error
 */
jobject createJavaSleepResult(JNIEnv* env, const SleepDetectionResult& result) {
    if (!g_sleepResultClass || !g_sleepResultConstructor) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
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

    if (result.wakeTime.has_value()) {
        auto wakeTimeEpoch = result.wakeTime.value().time_since_epoch();
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

        jlong durationMs = interruption.duration.count() * 60 * 1000; // Convert minutes to ms
        jint interactionType = static_cast<jint>(interruption.interactionType);

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
    jdouble qualityScore = result.qualityScore;
    jboolean isManuallyConfirmed = result.isManuallyConfirmed;

    jobject javaSleepResult = env->NewObject(g_sleepResultClass, g_sleepResultConstructor,
                                             bedtimeMs, wakeTimeMs, durationHours, confidence,
                                             interruptionsList, qualityScore, isManuallyConfirmed);

    // Clean up local references
    env->DeleteLocalRef(arrayListClass);
    env->DeleteLocalRef(interruptionsList);

    return javaSleepResult;
}

// JNI Method Implementations

extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_initializeNative(
        JNIEnv* env, jobject thiz) {

    JNIPerformanceTimer timer("initializeNative");

    std::lock_guard<std::mutex> lock(g_detectorMutex);

    try {
        // Initialize JNI references first
        if (!initializeJNIReferences(env)) {
            __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                                "Failed to initialize JNI references");
            return;
        }

        // Create sleep detector with default preferences
        UserPreferences defaultPrefs;
        defaultPrefs.targetSleepHours = std::chrono::duration<double, std::ratio<3600>>(8.0);
        defaultPrefs.minimumSleepDuration = std::chrono::hours(4);
        defaultPrefs.enableInterruptionTracking = true;
        defaultPrefs.enableSmartDetection = true;

        g_sleepDetector = std::make_unique<SleepDetector>(defaultPrefs);

        __android_log_print(ANDROID_LOG_INFO, "PuuyApu_JNI",
                            "Native sleep detector initialized successfully");

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Exception during initialization: %s", e.what());
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_addInteractionEvent(
        JNIEnv* env, jobject thiz,
        jlong timestamp, jint appType, jlong duration) {

    JNIPerformanceTimer timer("addInteractionEvent");

    if (!g_sleepDetector) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Sleep detector not initialized");
        return static_cast<jlong>(InteractionType::UNKNOWN);
    }

    try {
        // Create interaction event from Java parameters
        InteractionEvent event;
        event.timestamp = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(timestamp)
        );
        event.duration = std::chrono::milliseconds(duration);
        event.appCategory = static_cast<AppCategory>(appType);

        // Classify interaction type based on duration and context
        if (duration < 30000) { // Less than 30 seconds
            event.type = InteractionType::TIME_CHECK;
        } else if (duration < 300000) { // Less than 5 minutes
            event.type = InteractionType::MEANINGFUL_USE;
        } else {
            event.type = InteractionType::EXTENDED_SESSION;
        }

        // Add event to detector for processing
        g_sleepDetector->addInteractionEvent(event);

        return static_cast<jlong>(event.type);

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Exception in addInteractionEvent: %s", e.what());
        return static_cast<jlong>(InteractionType::UNKNOWN);
    }
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_detectSleep(
        JNIEnv* env, jobject thiz,
        jlongArray eventTimestamps, jintArray eventTypes) {

    JNIPerformanceTimer timer("detectSleep");

    if (!g_sleepDetector) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Sleep detector not initialized");
        return nullptr;
    }

    if (!eventTimestamps || !eventTypes) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Null arrays passed to detectSleep");
        return nullptr;
    }

    try {
        // Get array lengths and validate they match
        jsize timestampLength = env->GetArrayLength(eventTimestamps);
        jsize typeLength = env->GetArrayLength(eventTypes);

        if (timestampLength != typeLength) {
            __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                                "Array length mismatch: timestamps=%d, types=%d",
                                timestampLength, typeLength);
            return nullptr;
        }

        // Use RAII for safe array access
        SafeJNIArray<jlong> timestamps(env, eventTimestamps);
        SafeJNIArray<jint> types(env, eventTypes);

        if (!timestamps.isValid() || !types.isValid()) {
            __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                                "Failed to get array elements");
            return nullptr;
        }

        // Convert Java arrays to C++ vector (pre-allocate for performance)
        std::vector<InteractionEvent> events;
        events.reserve(timestampLength);

        for (jsize i = 0; i < timestampLength; i++) {
            InteractionEvent event;
            event.timestamp = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(timestamps.get()[i])
            );
            event.type = static_cast<InteractionType>(types.get()[i]);
            events.emplace_back(std::move(event));
        }

        // Perform sleep detection
        auto currentTime = std::chrono::system_clock::now();
        auto result = g_sleepDetector->detectSleepPeriod(currentTime);

        // Convert C++ result to Java object
        return createJavaSleepResult(env, result);

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Exception in detectSleep: %s", e.what());
        return nullptr;
    }
}

extern "C" JNIEXPORT jdouble JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_calculateConfidence(
        JNIEnv* env, jobject thiz,
        jlong bedtime, jlong wakeTime) {

    JNIPerformanceTimer timer("calculateConfidence");

    if (!g_sleepDetector) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Sleep detector not initialized");
        return 0.0;
    }

    try {
        // Create sleep result for confidence calculation
        SleepDetectionResult session;
        session.bedtime = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(bedtime)
        );
        session.wakeTime = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(wakeTime)
        );

        // Calculate duration
        if (session.bedtime.has_value() && session.wakeTime.has_value()) {
            session.duration = std::chrono::duration<double, std::ratio<3600>>(
                    calculateDurationHours(session.bedtime.value(), session.wakeTime.value())
            );
        }

        double confidence = g_sleepDetector->calculateConfidenceScore(session);
        return static_cast<jdouble>(confidence);

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Exception in calculateConfidence: %s", e.what());
        return 0.0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_updateUserPreferences(
        JNIEnv* env, jobject thiz,
        jdouble targetSleepHours, jlong preferredBedtime, jlong preferredWakeTime) {

    JNIPerformanceTimer timer("updateUserPreferences");

    if (!g_sleepDetector) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Sleep detector not initialized");
        return;
    }

    try {
        UserPreferences preferences;
        preferences.targetSleepHours = std::chrono::duration<double, std::ratio<3600>>(targetSleepHours);
        preferences.preferredBedtime = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(preferredBedtime)
        );
        preferences.preferredWakeTime = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(preferredWakeTime)
        );
        preferences.enableInterruptionTracking = true;
        preferences.enableSmartDetection = true;

        g_sleepDetector->updateUserPreferences(preferences);

        __android_log_print(ANDROID_LOG_INFO, "PuuyApu_JNI",
                            "User preferences updated: target=%.1f hours", targetSleepHours);

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Exception in updateUserPreferences: %s", e.what());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_processBatchInteractions(
        JNIEnv* env, jobject thiz,
        jlongArray timestamps, jintArray appTypes, jlongArray durations) {

    JNIPerformanceTimer timer("processBatchInteractions");

    if (!g_sleepDetector) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Sleep detector not initialized");
        return;
    }

    if (!timestamps || !appTypes || !durations) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Null arrays passed to processBatchInteractions");
        return;
    }

    try {
        jsize length = env->GetArrayLength(timestamps);

        // Validate all arrays have same length
        if (env->GetArrayLength(appTypes) != length ||
            env->GetArrayLength(durations) != length) {
            __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                                "Array length mismatch in processBatchInteractions");
            return;
        }

        // Use RAII for safe array access
        SafeJNIArray<jlong> timestampArray(env, timestamps);
        SafeJNIArray<jint> typeArray(env, appTypes);
        SafeJNIArray<jlong> durationArray(env, durations);

        if (!timestampArray.isValid() || !typeArray.isValid() || !durationArray.isValid()) {
            __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                                "Failed to get array elements in processBatchInteractions");
            return;
        }

        // Process events in batch for better performance
        for (jsize i = 0; i < length; i++) {
            InteractionEvent event;
            event.timestamp = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(timestampArray.get()[i])
            );
            event.appCategory = static_cast<AppCategory>(typeArray.get()[i]);
            event.duration = std::chrono::milliseconds(durationArray.get()[i]);

            // Classify interaction type
            jlong durationMs = durationArray.get()[i];
            if (durationMs < 30000) {
                event.type = InteractionType::TIME_CHECK;
            } else if (durationMs < 300000) {
                event.type = InteractionType::MEANINGFUL_USE;
            } else {
                event.type = InteractionType::EXTENDED_SESSION;
            }

            g_sleepDetector->addInteractionEvent(event);
        }

        __android_log_print(ANDROID_LOG_DEBUG, "PuuyApu_JNI",
                            "Processed batch of %d interaction events", length);

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Exception in processBatchInteractions: %s", e.what());
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_isCurrentlyAsleep(
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
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Exception in isCurrentlyAsleep: %s", e.what());
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_getEstimatedSleepStart(
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
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Exception in getEstimatedSleepStart: %s", e.what());
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_clearOldData(
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

        __android_log_print(ANDROID_LOG_DEBUG, "PuuyApu_JNI",
                            "Cleared data older than timestamp %lld", cutoffTimestamp);

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Exception in clearOldData: %s", e.what());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_optimizeMemory(
        JNIEnv* env, jobject thiz) {

    JNIPerformanceTimer timer("optimizeMemory");

    try {
        // Clear old performance metrics
        {
            std::lock_guard<std::mutex> lock(g_metricsMutex);
            g_jniMetrics.clear();
        }

        // Clear old data (keep last 7 days)
        auto cutoffTime = std::chrono::system_clock::now() - std::chrono::hours(24 * 7);
        if (g_sleepDetector) {
            g_sleepDetector->clearOldData(cutoffTime);
        }

        __android_log_print(ANDROID_LOG_INFO, "PuuyApu_JNI", "Memory optimization completed");

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Exception in optimizeMemory: %s", e.what());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_nava_puuyapu_native_NativeSleepTracker_confirmManualSleep(
        JNIEnv* env, jobject thiz, jlong timestamp) {

    JNIPerformanceTimer timer("confirmManualSleep");

    if (!g_sleepDetector) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Sleep detector not initialized");
        return;
    }

    try {
        auto sleepTime = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(timestamp)
        );

        g_sleepDetector->confirmManualSleep(sleepTime);

        __android_log_print(ANDROID_LOG_INFO, "PuuyApu_JNI",
                            "Manual sleep confirmation recorded at timestamp %lld", timestamp);

    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "PuuyApu_JNI",
                            "Exception in confirmManualSleep: %s", e.what());
    }
}

// Library lifecycle management

extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* reserved) {
    __android_log_print(ANDROID_LOG_INFO, "PuuyApu_JNI",
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

    __android_log_print(ANDROID_LOG_INFO, "PuuyApu_JNI",
                        "Native library unloaded successfully");
}