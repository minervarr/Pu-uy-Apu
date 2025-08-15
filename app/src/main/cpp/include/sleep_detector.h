/**
 * @file sleep_detector.h
 * @brief High-performance sleep detection engine for Puñuy Apu
 *
 * Core C++ engine that analyzes phone interaction patterns to automatically
 * detect sleep and wake times. Optimized for real-time processing with
 * microsecond-level performance targets.
 *
 * Thread-safe for concurrent access from background services.
 * Uses memory pooling and cache-friendly algorithms for optimal performance.
 *
 * @author Puñuy Apu Development Team
 * @version 1.0 - Phase 1A: C++ Core
 * @performance < 1ms for sleep detection, < 100μs for event processing
 */

#pragma once

#include "puuyapu_types.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <android/log.h>

namespace puuyapu {

/**
 * @brief High-performance sleep detection engine
 *
 * Core class that analyzes interaction patterns to automatically detect
 * sleep and wake times. Optimized for real-time processing with
 * microsecond-level performance targets.
 *
 * Key Features:
 * - Real-time interaction event processing
 * - Pattern-based sleep period detection
 * - Confidence scoring with multiple algorithms
 * - Memory-efficient circular buffer storage
 * - Thread-safe for background service integration
 * - Performance monitoring and optimization
 *
 * @performance
 * - Event processing: < 100 microseconds
 * - Sleep detection: < 1 millisecond (cached), < 5ms (full analysis)
 * - Confidence calculation: < 500 microseconds
 * - Memory usage: < 5MB for core algorithms
 */
    class SleepDetector {
    private:
        // Thread-safe event storage with circular buffer for memory efficiency
        mutable std::mutex events_mutex_;
        InteractionEventList recent_events_;
        size_t event_write_index_{0};
        bool event_buffer_full_{false};

        // User preferences (atomic pointer for lock-free reads)
        std::atomic<UserPreferences*> preferences_;
        std::unique_ptr<UserPreferences> preferences_storage_;

        // Performance monitoring
        mutable std::unordered_map<std::string, std::chrono::microseconds> performance_metrics_;
        mutable std::mutex metrics_mutex_;

        // Cache for expensive calculations
        mutable std::optional<SleepDetectionResult> cached_result_;
        mutable std::chrono::system_clock::time_point last_cache_update_;
        static constexpr std::chrono::minutes CACHE_VALIDITY_DURATION{5};

        // Memory management
        std::atomic<size_t> total_events_processed_{0};
        std::atomic<size_t> total_sleep_periods_detected_{0};

    public:
        /**
         * @brief Construct sleep detector with initial preferences
         * @param preferences User sleep preferences for personalized detection
         * @performance Target: < 1ms initialization time
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
         * Automatically classifies interaction type based on duration and context.
         *
         * @param event New interaction event to process
         * @performance Target: < 100 microseconds
         */
        void addInteractionEvent(const InteractionEvent& event) noexcept;

        /**
         * @brief Detect sleep period from recent interaction patterns
         *
         * Analyzes interaction history to identify sleep start and end times.
         * Uses multiple algorithms for robust detection:
         * - Gap analysis (periods without meaningful interaction)
         * - Pattern matching against user's historical sleep times
         * - Interaction type classification (time checks vs active use)
         * - Confidence scoring based on multiple factors
         *
         * @param current_time Current timestamp for real-time analysis
         * @return Sleep detection result with confidence score and details
         * @performance Target: < 1ms for cached results, < 5ms for full analysis
         */
        SleepDetectionResult detectSleepPeriod(
                const std::chrono::system_clock::time_point& current_time) const;

        /**
         * @brief Calculate confidence score for a sleep session
         *
         * Evaluates detection reliability based on multiple factors:
         * - Manual confirmation weight (highest priority)
         * - Pattern consistency with user's historical sleep times
         * - Sleep duration reasonableness (not too short/long)
         * - Number and type of interruptions during sleep
         * - Time of day alignment with typical sleep hours
         *
         * @param session Sleep session to evaluate
         * @return Confidence score from 0.0 (very low) to 1.0 (very high)
         * @performance Target: < 500 microseconds
         */
        double calculateConfidenceScore(const SleepDetectionResult& session) const noexcept;

        /**
         * @brief Update user preferences for personalized detection
         *
         * Thread-safe update of user sleep preferences. New preferences
         * take effect immediately for subsequent detections. Validates
         * preferences before applying to ensure reasonable values.
         *
         * @param new_preferences Updated user preferences
         * @performance Target: < 200 microseconds
         */
        void updateUserPreferences(const UserPreferences& new_preferences) noexcept;

        /**
         * @brief Check if user appears to be currently sleeping
         *
         * Real-time analysis of recent activity to determine current sleep state.
         * Useful for live dashboard updates and smart notifications.
         * Uses fast heuristics to avoid expensive full detection.
         *
         * @param current_time Current timestamp
         * @return true if user appears to be sleeping based on recent inactivity
         * @performance Target: < 100 microseconds
         */
        bool isCurrentlyAsleep(const std::chrono::system_clock::time_point& current_time) const noexcept;

        /**
         * @brief Get estimated sleep start time if currently sleeping
         *
         * Returns best estimate of when current sleep period began.
         * Uses gap analysis and pattern matching for accuracy.
         * Returns nullopt if user is not currently sleeping.
         *
         * @param current_time Current timestamp
         * @return Optional sleep start time, or nullopt if not sleeping
         * @performance Target: < 200 microseconds
         */
        std::optional<std::chrono::system_clock::time_point> getEstimatedSleepStart(
                const std::chrono::system_clock::time_point& current_time) const noexcept;

        /**
         * @brief Clear old interaction data to manage memory usage
         *
         * Removes interaction events older than specified cutoff time.
         * Maintains recent history for pattern analysis while preventing
         * unbounded memory growth. Thread-safe operation.
         *
         * @param cutoff_time Remove events older than this timestamp
         * @performance Target: < 1 millisecond
         */
        void clearOldData(const std::chrono::system_clock::time_point& cutoff_time) noexcept;

        /**
         * @brief Get performance metrics for monitoring and optimization
         *
         * Returns timing information for key operations to enable
         * performance monitoring and optimization in production.
         * Includes cache hit rates, average processing times, etc.
         *
         * @return Map of operation names to average execution times
         * @performance Target: < 50 microseconds
         */
        std::unordered_map<std::string, std::chrono::microseconds> getPerformanceMetrics() const;

        /**
         * @brief Force manual sleep confirmation for improved accuracy
         *
         * Records user's manual "Going to Sleep" confirmation with timestamp.
         * Manual confirmations receive highest weight in confidence calculations.
         * Invalidates cache to ensure immediate effect.
         *
         * @param timestamp Time when user confirmed going to sleep
         * @performance Target: < 100 microseconds
         */
        void confirmManualSleep(const std::chrono::system_clock::time_point& timestamp) noexcept;

        /**
         * @brief Get statistics about detection performance
         *
         * Returns information about detection accuracy, event processing rates,
         * and other performance statistics for monitoring and debugging.
         *
         * @return Statistics object with performance data
         */
        struct Statistics {
            size_t total_events_processed;
            size_t total_sleep_periods_detected;
            double average_confidence_score;
            std::chrono::microseconds average_detection_time;
            double cache_hit_rate;
            size_t current_memory_usage_bytes;
        };

        Statistics getStatistics() const noexcept;

        /**
         * @brief Optimize memory usage and performance
         *
         * Performs maintenance operations:
         * - Clears old cached data
         * - Optimizes internal data structures
         * - Resets performance counters
         * - Triggers garbage collection of unused objects
         *
         * @performance Target: < 10 milliseconds
         */
        void optimizeMemory() noexcept;

    private:
        // Internal helper methods for sleep detection algorithms

        /**
         * @brief Find the last meaningful interaction before sleep
         * @param events Vector of interaction events to analyze
         * @param current_time Current time for relative analysis
         * @return Optional timestamp of last meaningful interaction
         * @performance Target: < 500 microseconds
         */
        std::optional<std::chrono::system_clock::time_point> findSleepStartTime(
                const InteractionEventList& events,
                const std::chrono::system_clock::time_point& current_time) const noexcept;

        /**
         * @brief Find first meaningful interaction after sleep period
         * @param events Vector of interaction events to analyze
         * @param sleep_start Estimated sleep start time
         * @param current_time Current timestamp for analysis
         * @return Optional timestamp of sleep end
         * @performance Target: < 500 microseconds
         */
        std::optional<std::chrono::system_clock::time_point> findSleepEndTime(
                const InteractionEventList& events,
                const std::chrono::system_clock::time_point& sleep_start,
                const std::chrono::system_clock::time_point& current_time) const noexcept;

        /**
         * @brief Analyze sleep interruptions during sleep period
         * @param events All interaction events
         * @param sleep_start Beginning of sleep period
         * @param sleep_end End of sleep period
         * @return Vector of detected sleep interruptions
         * @performance Target: < 1 millisecond
         */
        SleepInterruptionList analyzeInterruptions(
                const InteractionEventList& events,
                const std::chrono::system_clock::time_point& sleep_start,
                const std::chrono::system_clock::time_point& sleep_end) const noexcept;

        /**
         * @brief Check if current detection can use cached result
         * @param current_time Current timestamp
         * @return true if cached result is still valid
         * @performance Target: < 20 microseconds
         */
        bool canUseCachedResult(const std::chrono::system_clock::time_point& current_time) const noexcept;

        /**
         * @brief Evaluate how well sleep timing matches user's typical pattern
         * @param sleep_start Detected sleep start time
         * @param sleep_end Detected sleep end time
         * @return Pattern consistency score 0.0-1.0
         * @performance Target: < 300 microseconds
         */
        double evaluatePatternConsistency(
                const std::chrono::system_clock::time_point& sleep_start,
                const std::chrono::system_clock::time_point& sleep_end) const noexcept;

        /**
         * @brief Record performance metric for monitoring
         * @param operation Name of operation being measured
         * @param duration Time taken for operation
         * @performance Target: < 10 microseconds
         */
        void recordPerformanceMetric(const std::string& operation,
                                     std::chrono::microseconds duration) const noexcept;

        /**
         * @brief Get events in chronological order for analysis
         * @return Sorted copy of recent events (thread-safe)
         * @performance Target: < 1 millisecond
         */
        InteractionEventList getSortedEvents() const;

        /**
         * @brief Detect interaction gaps that might represent sleep
         * @param events Sorted interaction events
         * @param min_gap_duration Minimum gap to consider
         * @return List of time gaps found
         * @performance Target: < 500 microseconds
         */
        TimeGapList detectInteractionGaps(
                const InteractionEventList& events,
                std::chrono::milliseconds min_gap_duration) const noexcept;

        /**
         * @brief Classify interaction as time check vs meaningful use
         * @param event Event to classify
         * @param recent_history Recent events for context
         * @return Refined interaction type
         * @performance Target: < 50 microseconds
         */
        InteractionType classifyInteraction(
                const InteractionEvent& event,
                const InteractionEventList& recent_history) const noexcept;

        /**
         * @brief Check if interaction represents brief time check
         * @param event Event to analyze
         * @return true if likely time check
         * @performance Target: < 10 microseconds
         */
        bool isTimeCheck(const InteractionEvent& event) const noexcept;

        /**
         * @brief Check if interaction represents meaningful usage
         * @param event Event to analyze
         * @return true if meaningful phone usage
         * @performance Target: < 10 microseconds
         */
        bool isMeaningfulUsage(const InteractionEvent& event) const noexcept;

        /**
         * @brief Calculate sleep quality score based on interruptions
         * @param interruptions List of sleep interruptions
         * @param total_sleep_duration Total sleep duration
         * @return Quality score 0.0-1.0
         * @performance Target: < 200 microseconds
         */
        double calculateSleepQuality(
                const SleepInterruptionList& interruptions,
                std::chrono::milliseconds total_sleep_duration) const noexcept;
    };

/**
 * @brief Interaction analyzer for real-time event processing
 *
 * Specialized class for fast interaction classification and analysis.
 * Used by SleepDetector for real-time processing of phone usage events.
 */
    class InteractionAnalyzer {
    public:
        /**
         * @brief Classify interaction types in microseconds
         * @param event Interaction event to classify
         * @param recent_history Recent events for context
         * @return Classified interaction type
         * @performance Target: < 50 microseconds
         */
        static InteractionType classifyInteraction(
                const InteractionEvent& event,
                const InteractionEventList& recent_history) noexcept;

        /**
         * @brief Fast gap detection between interactions
         * @param events Vector of interaction events (must be sorted)
         * @param minimum_gap Minimum gap duration to detect
         * @return Vector of detected time gaps
         * @performance Target: < 500 microseconds
         */
        static TimeGapList detectInteractionGaps(
                const InteractionEventList& events,
                std::chrono::milliseconds minimum_gap) noexcept;

        /**
         * @brief Real-time pattern recognition for sleep detection
         * @param events Recent interaction events
         * @param preferences User sleep preferences
         * @return true if pattern suggests user is sleeping
         * @performance Target: < 200 microseconds
         */
        static bool detectSleepPattern(
                const InteractionEventList& events,
                const UserPreferences& preferences) noexcept;

        /**
         * @brief Check if interaction is likely time check
         * @param event Interaction to analyze
         * @return true if likely brief time check
         * @performance Target: < 10 microseconds
         */
        static bool isTimeCheck(const InteractionEvent& event) noexcept;

        /**
         * @brief Check if interaction represents meaningful usage
         * @param event Interaction to analyze
         * @return true if meaningful phone usage
         * @performance Target: < 10 microseconds
         */
        static bool isMeaningfulUsage(const InteractionEvent& event) noexcept;
    };

// Global utility functions for time calculations and conversions

/**
 * @brief Calculate duration between two time points in hours
 * @param start Start time point
 * @param end End time point
 * @return Duration in hours as double precision
 * @performance Target: < 20 microseconds
 */
    inline double calculateDurationHours(
            const std::chrono::system_clock::time_point& start,
            const std::chrono::system_clock::time_point& end) noexcept {

        auto duration = end - start;
        return std::chrono::duration<double, std::ratio<3600>>(duration).count();
    }

/**
 * @brief Check if time point falls within a daily time range
 * @param time_point Time to check
 * @param range_start Start of daily time range (minutes since midnight)
 * @param range_end End of daily time range (minutes since midnight)
 * @return true if time point is within the specified daily range
 * @performance Target: < 50 microseconds
 */
    bool isWithinDailyTimeRange(
            const std::chrono::system_clock::time_point& time_point,
            std::chrono::minutes range_start,
            std::chrono::minutes range_end) noexcept;

/**
 * @brief Convert system time point to minutes since midnight
 * @param time_point Time point to convert
 * @return Minutes since midnight (0-1439)
 * @performance Target: < 30 microseconds
 */
    std::chrono::minutes getMinutesSinceMidnight(
            const std::chrono::system_clock::time_point& time_point) noexcept;

/**
 * @brief Check if time point represents nighttime hours
 * @param time_point Time to check
 * @return true if between 22:00 and 06:00
 * @performance Target: < 30 microseconds
 */
    bool isNighttime(const std::chrono::system_clock::time_point& time_point) noexcept;

// Performance monitoring and logging macros
#ifdef DEBUG
    #define SLEEP_LOG_DEBUG(tag, format, ...) \
        __android_log_print(ANDROID_LOG_DEBUG, tag, format, ##__VA_ARGS__)
    #define SLEEP_LOG_INFO(tag, format, ...) \
        __android_log_print(ANDROID_LOG_INFO, tag, format, ##__VA_ARGS__)
    #define SLEEP_LOG_PERF(operation, duration_us) \
        __android_log_print(ANDROID_LOG_DEBUG, "PuuyApu_Perf", \
            "%s took %ld microseconds", operation, duration_us)
#else
#define SLEEP_LOG_DEBUG(tag, format, ...)
#define SLEEP_LOG_INFO(tag, format, ...)
#define SLEEP_LOG_PERF(operation, duration_us)
#endif

#define SLEEP_LOG_ERROR(tag, format, ...) \
    __android_log_print(ANDROID_LOG_ERROR, tag, format, ##__VA_ARGS__)

/**
 * @brief RAII performance timer for automatic measurement
 *
 * Automatically measures execution time and logs performance data.
 * Use for debugging and optimization of critical code paths.
 */
    class PerformanceTimer {
    private:
        std::string operation_name_;
        std::chrono::high_resolution_clock::time_point start_time_;

    public:
        explicit PerformanceTimer(const std::string& operation)
                : operation_name_(operation), start_time_(std::chrono::high_resolution_clock::now()) {}

        ~PerformanceTimer() {
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                    end_time - start_time_
            );
            SLEEP_LOG_PERF(operation_name_.c_str(), duration.count());
        }
    };

// Convenience macro for performance measurement
#ifdef DEBUG
#define MEASURE_PERFORMANCE(operation) PerformanceTimer _timer(operation)
#else
#define MEASURE_PERFORMANCE(operation)
#endif

} // namespace puuyapu