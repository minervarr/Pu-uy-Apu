// sleep_detector.h - Optimized high-performance sleep detection engine
#pragma once

#include <chrono>
#include <vector>
#include <optional>
#include <memory>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <android/log.h>

// Performance optimization: Use compile-time constants
namespace Constants {
    constexpr std::chrono::hours MIN_SLEEP_DURATION{4};
    constexpr std::chrono::minutes INTERACTION_TIMEOUT{30};
    constexpr double HIGH_CONFIDENCE_THRESHOLD = 0.8;
    constexpr double MEDIUM_CONFIDENCE_THRESHOLD = 0.5;
    constexpr size_t MAX_EVENTS_CACHE = 10000;
}

/**
 * @brief Enumeration for different types of phone interactions
 *
 * Categorizes user interactions to distinguish between meaningful usage
 * and brief checks (like time checking) for accurate sleep detection.
 */
enum class InteractionType : uint8_t {
    UNKNOWN = 0,
    TIME_CHECK = 1,           // Brief screen activation < 30 seconds
    NOTIFICATION_CHECK = 2,    // Quick notification glance
    MEANINGFUL_USE = 3,        // Extended app usage > 30 seconds
    APP_LAUNCH = 4,           // New application started
    EXTENDED_SESSION = 5      // Continuous usage > 5 minutes
};

/**
 * @brief App category classification for context-aware detection
 *
 * Different app types have different implications for sleep detection.
 * Entertainment apps used late suggest active wakefulness, while
 * alarm/clock apps might indicate sleep preparation.
 */
enum class AppCategory : uint8_t {
    UNKNOWN = 0,
    COMMUNICATION = 1,        // WhatsApp, SMS, calls
    ENTERTAINMENT = 2,        // YouTube, Netflix, games
    PRODUCTIVITY = 3,         // Email, documents, work apps
    SYSTEM = 4,              // Settings, clock, alarm
    HEALTH = 5,              // Sleep apps, fitness trackers
    SOCIAL_MEDIA = 6         // Instagram, Facebook, Twitter
};

/**
 * @brief Sleep detection confidence levels
 *
 * Indicates reliability of sleep period detection based on
 * interaction patterns, manual confirmations, and historical data.
 */
enum class SleepConfidence : uint8_t {
    LOW = 0,      // Uncertain detection, possible false positive
    MEDIUM = 1,   // Good pattern match, likely accurate
    HIGH = 2      // Strong confidence with manual confirmation or clear pattern
};

/**
 * @brief Individual interaction event with optimized memory layout
 *
 * Represents a single phone interaction event. Memory layout is optimized
 * for cache efficiency with most frequently accessed fields first.
 */
struct InteractionEvent {
    std::chrono::system_clock::time_point timestamp;  // 8 bytes - most important
    std::chrono::milliseconds duration;                // 8 bytes
    InteractionType type;                              // 1 byte
    AppCategory appCategory;                           // 1 byte
    uint16_t appId;                                   // 2 bytes - for app identification

    // Padding to align to 8-byte boundary for better cache performance
    uint32_t _padding;

    /**
     * @brief Check if this interaction represents meaningful phone usage
     * @return true if interaction indicates active phone use vs brief check
     */
    inline bool isMeaningfulUsage() const noexcept {
        return type == InteractionType::MEANINGFUL_USE ||
               type == InteractionType::APP_LAUNCH ||
               type == InteractionType::EXTENDED_SESSION;
    }

    /**
     * @brief Check if interaction duration exceeds time check threshold
     * @return true if interaction lasted longer than typical time check
     */
    inline bool exceedsTimeCheckDuration() const noexcept {
        return duration > Constants::INTERACTION_TIMEOUT;
    }
};

/**
 * @brief Sleep interruption during a sleep period
 *
 * Represents brief awakenings during sleep that don't constitute
 * the end of the sleep session. Important for sleep quality analysis.
 */
struct SleepInterruption {
    std::chrono::system_clock::time_point timestamp;
    std::chrono::minutes duration;
    InteractionType interactionType;
    bool isTimeCheck;           // True if likely just checking time
    double impactScore;         // 0.0-1.0, impact on sleep quality
};

/**
 * @brief Complete sleep detection result with confidence metrics
 *
 * Contains all information about a detected sleep period including
 * timing, quality metrics, and confidence assessment.
 */
struct SleepDetectionResult {
    std::optional<std::chrono::system_clock::time_point> bedtime;
    std::optional<std::chrono::system_clock::time_point> wakeTime;
    std::chrono::duration<double, std::ratio<3600>> duration{0}; // Hours as double
    SleepConfidence confidence{SleepConfidence::LOW};
    std::vector<SleepInterruption> interruptions;
    double qualityScore{0.0};      // 0.0-1.0, overall sleep quality
    bool isManuallyConfirmed{false}; // User pressed "Going to Sleep" button

    /**
     * @brief Check if sleep detection result is valid
     * @return true if both bedtime and wake time are detected and duration > minimum
     */
    bool isValid() const noexcept {
        return bedtime.has_value() &&
               wakeTime.has_value() &&
               duration >= Constants::MIN_SLEEP_DURATION;
    }

    /**
     * @brief Get human-readable confidence description
     * @return string representation of confidence level
     */
    const char* getConfidenceString() const noexcept {
        switch (confidence) {
            case SleepConfidence::HIGH: return "High";
            case SleepConfidence::MEDIUM: return "Medium";
            case SleepConfidence::LOW: return "Low";
            default: return "Unknown";
        }
    }
};

/**
 * @brief User sleep preferences for personalized detection
 *
 * Stores user's sleep goals and patterns to improve detection accuracy
 * through personalization and historical pattern matching.
 */
struct UserPreferences {
    std::chrono::duration<double, std::ratio<3600>> targetSleepHours{8.0};
    std::chrono::system_clock::time_point preferredBedtime;
    std::chrono::system_clock::time_point preferredWakeTime;
    std::chrono::hours minimumSleepDuration{4};
    bool enableInterruptionTracking{true};
    bool enableSmartDetection{true};

    // Weekday vs weekend different schedules
    bool hasWeekendSchedule{false};
    std::chrono::system_clock::time_point weekendBedtime;
    std::chrono::system_clock::time_point weekendWakeTime;
};

/**
 * @brief High-performance sleep detection engine
 *
 * Core class that analyzes interaction patterns to automatically detect
 * sleep and wake times. Optimized for real-time processing with
 * microsecond-level performance targets.
 *
 * Thread-safe for concurrent access from background services.
 * Uses memory pooling and cache-friendly algorithms for optimal performance.
 */
class SleepDetector {
private:
    // Thread-safe event storage with circular buffer for memory efficiency
    mutable std::mutex eventsMutex_;
    std::vector<InteractionEvent> recentEvents_;
    size_t eventWriteIndex_{0};
    bool eventBufferFull_{false};

    // User preferences (atomic for lock-free reads)
    std::atomic<UserPreferences*> preferences_;

    // Performance monitoring
    mutable std::unordered_map<std::string, std::chrono::microseconds> performanceMetrics_;
    mutable std::mutex metricsMutex_;

    // Cache for expensive calculations
    mutable std::optional<SleepDetectionResult> cachedResult_;
    mutable std::chrono::system_clock::time_point lastCacheUpdate_;
    static constexpr std::chrono::minutes CACHE_VALIDITY_DURATION{5};

public:
    /**
     * @brief Construct sleep detector with initial preferences
     * @param preferences User sleep preferences for personalized detection
     */
    explicit SleepDetector(const UserPreferences& preferences);

    /**
     * @brief Destructor - ensures proper cleanup of resources
     */
    ~SleepDetector() noexcept;

    // Disable copy constructor and assignment for performance
    SleepDetector(const SleepDetector&) = delete;
    SleepDetector& operator=(const SleepDetector&) = delete;

    // Enable move semantics for efficient transfers
    SleepDetector(SleepDetector&&) noexcept = default;
    SleepDetector& operator=(SleepDetector&&) noexcept = default;

    /**
     * @brief Add new interaction event for processing
     *
     * Thread-safe method to add interaction events. Uses circular buffer
     * to maintain bounded memory usage while preserving recent history.
     *
     * @param event New interaction event to process
     * @performance Target: < 100 microseconds
     */
    void addInteractionEvent(const InteractionEvent& event) noexcept;

    /**
     * @brief Detect sleep period from recent interaction patterns
     *
     * Analyzes interaction history to identify sleep start and end times.
     * Uses multiple algorithms for robust detection including:
     * - Gap analysis (periods without meaningful interaction)
     * - Pattern matching against user's historical sleep times
     * - Interaction type classification (time checks vs active use)
     *
     * @param currentTime Current timestamp for real-time analysis
     * @return Sleep detection result with confidence score
     * @performance Target: < 1 millisecond for cached results, < 5ms for full analysis
     */
    SleepDetectionResult detectSleepPeriod(
            const std::chrono::system_clock::time_point& currentTime) const;

    /**
     * @brief Calculate confidence score for a sleep session
     *
     * Evaluates detection reliability based on multiple factors:
     * - Manual confirmation weight (highest)
     * - Pattern consistency with user's history
     * - Sleep duration reasonableness
     * - Number and type of interruptions
     *
     * @param session Sleep session to evaluate
     * @return Confidence score from 0.0 (low) to 1.0 (high)
     * @performance Target: < 500 microseconds
     */
    double calculateConfidenceScore(const SleepDetectionResult& session) const noexcept;

    /**
     * @brief Update user preferences for personalized detection
     *
     * Thread-safe update of user sleep preferences. New preferences
     * take effect immediately for subsequent detections.
     *
     * @param newPreferences Updated user preferences
     */
    void updateUserPreferences(const UserPreferences& newPreferences) noexcept;

    /**
     * @brief Check if user appears to be currently sleeping
     *
     * Real-time analysis of recent activity to determine current sleep state.
     * Useful for live dashboard updates and notifications.
     *
     * @param currentTime Current timestamp
     * @return true if user appears to be sleeping based on recent inactivity
     * @performance Target: < 100 microseconds
     */
    bool isCurrentlyAsleep(const std::chrono::system_clock::time_point& currentTime) const noexcept;

    /**
     * @brief Get estimated sleep start time if currently sleeping
     *
     * Returns best estimate of when current sleep period began.
     * Returns nullopt if user is not currently sleeping.
     *
     * @param currentTime Current timestamp
     * @return Optional sleep start time
     */
    std::optional<std::chrono::system_clock::time_point> getEstimatedSleepStart(
            const std::chrono::system_clock::time_point& currentTime) const noexcept;

    /**
     * @brief Clear old interaction data to manage memory usage
     *
     * Removes interaction events older than specified cutoff time.
     * Maintains recent history for pattern analysis while preventing
     * unbounded memory growth.
     *
     * @param cutoffTime Remove events older than this timestamp
     * @performance Target: < 1 millisecond
     */
    void clearOldData(const std::chrono::system_clock::time_point& cutoffTime) noexcept;

    /**
     * @brief Get performance metrics for monitoring and optimization
     *
     * Returns timing information for key operations to enable
     * performance monitoring and optimization in production.
     *
     * @return Map of operation names to average execution times
     */
    std::unordered_map<std::string, std::chrono::microseconds> getPerformanceMetrics() const;

    /**
     * @brief Force manual sleep confirmation for improved accuracy
     *
     * Records user's manual "Going to Sleep" confirmation with timestamp.
     * Manual confirmations receive highest weight in confidence calculations.
     *
     * @param timestamp Time when user confirmed going to sleep
     */
    void confirmManualSleep(const std::chrono::system_clock::time_point& timestamp) noexcept;

private:
    // Internal helper methods for sleep detection algorithms

    /**
     * @brief Find the last meaningful interaction before sleep
     * @param events Vector of interaction events to analyze
     * @return Optional timestamp of last meaningful interaction
     */
    std::optional<std::chrono::system_clock::time_point> findSleepStartTime(
            const std::vector<InteractionEvent>& events) const noexcept;

    /**
     * @brief Find first meaningful interaction after sleep period
     * @param events Vector of interaction events to analyze
     * @param sleepStart Estimated sleep start time
     * @param currentTime Current timestamp for analysis
     * @return Optional timestamp of sleep end
     */
    std::optional<std::chrono::system_clock::time_point> findSleepEndTime(
            const std::vector<InteractionEvent>& events,
            const std::chrono::system_clock::time_point& sleepStart,
            const std::chrono::system_clock::time_point& currentTime) const noexcept;

    /**
     * @brief Analyze sleep interruptions during sleep period
     * @param events All interaction events
     * @param sleepStart Beginning of sleep period
     * @param sleepEnd End of sleep period
     * @return Vector of detected sleep interruptions
     */
    std::vector<SleepInterruption> analyzeInterruptions(
            const std::vector<InteractionEvent>& events,
            const std::chrono::system_clock::time_point& sleepStart,
            const std::chrono::system_clock::time_point& sleepEnd) const noexcept;

    /**
     * @brief Check if current detection can use cached result
     * @param currentTime Current timestamp
     * @return true if cached result is still valid
     */
    bool canUseCachedResult(const std::chrono::system_clock::time_point& currentTime) const noexcept;

    /**
     * @brief Evaluate how well sleep timing matches user's typical pattern
     * @param sleepStart Detected sleep start time
     * @param sleepEnd Detected sleep end time
     * @return Pattern consistency score 0.0-1.0
     */
    double evaluatePatternConsistency(
            const std::chrono::system_clock::time_point& sleepStart,
            const std::chrono::system_clock::time_point& sleepEnd) const noexcept;

    /**
     * @brief Record performance metric for monitoring
     * @param operation Name of operation being measured
     * @param duration Time taken for operation
     */
    void recordPerformanceMetric(const std::string& operation,
                                 std::chrono::microseconds duration) const noexcept;
};

// Global utility functions for time calculations

/**
 * @brief Calculate duration between two time points in hours
 * @param start Start time point
 * @param end End time point
 * @return Duration in hours as double precision
 */
inline double calculateDurationHours(
        const std::chrono::system_clock::time_point& start,
        const std::chrono::system_clock::time_point& end) noexcept {

    auto duration = end - start;
    return std::chrono::duration<double, std::ratio<3600>>(duration).count();
}

/**
 * @brief Check if time point falls within a daily time range
 * @param timePoint Time to check
 * @param rangeStart Start of daily time range
 * @param rangeEnd End of daily time range
 * @return true if time point is within the specified daily range
 */
bool isWithinDailyTimeRange(
        const std::chrono::system_clock::time_point& timePoint,
        const std::chrono::system_clock::time_point& rangeStart,
        const std::chrono::system_clock::time_point& rangeEnd) noexcept;

// Logging macros for C++ debug builds
#ifdef DEBUG
#define SLEEP_LOG_DEBUG(tag, format, ...) \
        __android_log_print(ANDROID_LOG_DEBUG, tag, format, ##__VA_ARGS__)
    #define SLEEP_LOG_INFO(tag, format, ...) \
        __android_log_print(ANDROID_LOG_INFO, tag, format, ##__VA_ARGS__)
#else
#define SLEEP_LOG_DEBUG(tag, format, ...)
#define SLEEP_LOG_INFO(tag, format, ...)
#endif

#define SLEEP_LOG_ERROR(tag, format, ...) \
    __android_log_print(ANDROID_LOG_ERROR, tag, format, ##__VA_ARGS__)