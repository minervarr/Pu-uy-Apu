/**
 * @file puuyapu_types.h
 * @brief Core data types and structures for Puñuy Apu Sleep Tracker
 *
 * High-performance C++ types optimized for Android NDK.
 * All sleep detection processing happens in C++ for maximum speed.
 *
 * @author Puñuy Apu Development Team
 * @version 1.0 - Phase 1A: C++ Core
 * @performance Target: < 1ms for sleep detection algorithms
 */

#pragma once

#include <chrono>
#include <vector>
#include <optional>
#include <memory>
#include <string>
#include <cstdint>

namespace puuyapu {

/**
 * @brief Phone interaction types for sleep pattern analysis
 *
 * Categorizes user interactions to distinguish between meaningful usage
 * and brief checks for accurate sleep detection.
 * Memory optimized: uses uint8_t for minimal footprint.
 */
    enum class InteractionType : uint8_t {
        UNKNOWN = 0,
        TIME_CHECK = 1,          ///< Brief screen check < 30 seconds
        MEANINGFUL_USE = 2,       ///< Active app usage > 30 seconds
        NOTIFICATION_RESPONSE = 3, ///< Responding to notifications
        EXTENDED_USE = 4,         ///< Long session > 5 minutes
        SLEEP_CONFIRMATION = 5    ///< Manual "Going to Sleep" button
    };

/**
 * @brief App category classification for context-aware detection
 *
 * Different app types have different implications for sleep detection.
 * Social media late at night suggests active use vs alarm apps suggesting bedtime.
 */
    enum class AppCategory : uint8_t {
        UNKNOWN = 0,
        SOCIAL_MEDIA = 1,    ///< Facebook, Instagram, Twitter
        MESSAGING = 2,       ///< WhatsApp, Telegram, SMS
        ENTERTAINMENT = 3,   ///< YouTube, Netflix, games
        PRODUCTIVITY = 4,    ///< Email, calendar, notes
        CLOCK_ALARM = 5,     ///< Clock, alarm, weather apps
        SYSTEM = 6           ///< Settings, system apps
    };

/**
 * @brief Sleep detection confidence levels
 *
 * Indicates reliability of sleep period detection based on
 * interaction patterns, manual confirmations, and historical data.
 */
    enum class SleepConfidence : uint8_t {
        VERY_LOW = 0,    ///< < 30% confidence, likely false positive
        LOW = 1,         ///< 30-50% confidence, uncertain detection
        MEDIUM = 2,      ///< 50-75% confidence, probable sleep
        HIGH = 3,        ///< 75-90% confidence, very likely sleep
        VERY_HIGH = 4    ///< 90%+ confidence, manual confirmation or strong pattern
    };

/**
 * @brief High-performance interaction event structure
 *
 * Represents a single phone interaction event.
 * Memory layout optimized for cache efficiency (32 bytes aligned).
 *
 * @performance All operations < 100 microseconds
 */
    struct InteractionEvent {
        std::chrono::system_clock::time_point timestamp;  ///< When interaction occurred (8 bytes)
        std::chrono::milliseconds duration;                ///< How long interaction lasted (8 bytes)
        InteractionType type;                              ///< Classification of interaction (1 byte)
        AppCategory category;                              ///< App category if known (1 byte)
        uint16_t app_hash;                                ///< Hash of package name (2 bytes)
        uint32_t session_id;                              ///< Session identifier (4 bytes)
        uint64_t user_interaction_count;                  ///< Total interactions in session (8 bytes)

        // Constructors optimized for performance
        InteractionEvent() = default;

        /**
         * @brief Fast constructor for real-time event creation
         * @param ts Event timestamp
         * @param dur Duration of interaction
         * @param t Type of interaction
         * @param cat App category (optional)
         */
        InteractionEvent(
                std::chrono::system_clock::time_point ts,
                std::chrono::milliseconds dur,
                InteractionType t,
                AppCategory cat = AppCategory::UNKNOWN
        ) noexcept : timestamp(ts), duration(dur), type(t), category(cat),
                     app_hash(0), session_id(0), user_interaction_count(0) {}

        // Fast comparison operators for sorting and searching
        bool operator<(const InteractionEvent& other) const noexcept {
            return timestamp < other.timestamp;
        }

        bool operator==(const InteractionEvent& other) const noexcept {
            return timestamp == other.timestamp &&
                   duration == other.duration &&
                   type == other.type;
        }

        /**
         * @brief Check if this represents a brief time check vs meaningful use
         * @return true if likely just checking time/notifications briefly
         * @performance < 10 microseconds
         */
        inline bool isTimeCheck() const noexcept {
            return type == InteractionType::TIME_CHECK ||
                   (category == AppCategory::CLOCK_ALARM && duration.count() < 30000) ||
                   duration.count() < 15000; // Less than 15 seconds is likely time check
        }

        /**
         * @brief Check if this represents meaningful phone usage
         * @return true if interaction indicates active phone use
         * @performance < 10 microseconds
         */
        inline bool isMeaningfulUse() const noexcept {
            return type == InteractionType::MEANINGFUL_USE ||
                   type == InteractionType::EXTENDED_USE ||
                   type == InteractionType::NOTIFICATION_RESPONSE ||
                   duration.count() >= 30000; // 30+ seconds is meaningful
        }

        /**
         * @brief Check if this interaction is sleep-related
         * @return true if interaction suggests sleep preparation
         * @performance < 10 microseconds
         */
        inline bool isSleepRelated() const noexcept {
            return type == InteractionType::SLEEP_CONFIRMATION ||
                   (category == AppCategory::CLOCK_ALARM && duration.count() < 10000);
        }
    };

/**
 * @brief Sleep interruption during a sleep period
 *
 * Represents brief awakenings during sleep that don't constitute
 * the end of the sleep session. Important for sleep quality analysis.
 */
    struct SleepInterruption {
        std::chrono::system_clock::time_point timestamp;   ///< When interruption occurred
        std::chrono::milliseconds duration;                 ///< How long user was awake
        InteractionType cause;                              ///< What caused the interruption
        AppCategory app_category;                           ///< App category if applicable
        bool is_brief_check;                               ///< True if likely just time check
        double impact_score;                               ///< 0.0-1.0, impact on sleep quality

        SleepInterruption() = default;

        SleepInterruption(
                std::chrono::system_clock::time_point ts,
                std::chrono::milliseconds dur,
                InteractionType c,
                AppCategory cat = AppCategory::UNKNOWN
        ) noexcept : timestamp(ts), duration(dur), cause(c), app_category(cat),
                     is_brief_check(dur.count() < 30000), impact_score(0.0) {

            // Calculate impact score based on duration and timing
            if (is_brief_check) {
                impact_score = 0.1; // Minimal impact for brief checks
            } else {
                // Longer interruptions have more impact
                impact_score = std::min(1.0, dur.count() / (10.0 * 60 * 1000)); // Max impact at 10 minutes
            }
        }
    };

/**
 * @brief Complete sleep detection result with confidence metrics
 *
 * Contains all information about a detected sleep period including
 * timing, quality metrics, and confidence assessment.
 */
    struct SleepDetectionResult {
        std::optional<std::chrono::system_clock::time_point> bedtime;     ///< When sleep started
        std::optional<std::chrono::system_clock::time_point> wake_time;   ///< When sleep ended
        std::chrono::duration<double, std::ratio<3600>> duration{0};      ///< Sleep duration in hours
        SleepConfidence confidence{SleepConfidence::LOW};                 ///< Detection confidence
        std::vector<SleepInterruption> interruptions;                     ///< Mid-sleep wake-ups
        double quality_score{0.0};                                        ///< 0.0-1.0, overall sleep quality
        bool is_manually_confirmed{false};                                ///< User pressed "Going to Sleep" button
        double pattern_match_score{0.0};                                  ///< How well this matches user's typical pattern

        /**
         * @brief Check if this sleep detection result is valid and usable
         * @return true if both bedtime and wake time detected with reasonable duration
         * @performance < 50 microseconds
         */
        bool isValid() const noexcept {
            return bedtime.has_value() &&
                   wake_time.has_value() &&
                   duration.count() >= 1.0 &&     // At least 1 hour
                   duration.count() <= 24.0;      // Max 24 hours
        }

        /**
         * @brief Get human-readable confidence description
         * @return C-string description of confidence level
         * @performance < 10 microseconds
         */
        const char* getConfidenceString() const noexcept {
            switch (confidence) {
                case SleepConfidence::VERY_HIGH: return "Very High";
                case SleepConfidence::HIGH: return "High";
                case SleepConfidence::MEDIUM: return "Medium";
                case SleepConfidence::LOW: return "Low";
                case SleepConfidence::VERY_LOW: return "Very Low";
                default: return "Unknown";
            }
        }

        /**
         * @brief Calculate sleep efficiency (actual sleep vs time in bed)
         * @return Efficiency ratio 0.0-1.0
         * @performance < 100 microseconds
         */
        double calculateSleepEfficiency() const noexcept {
            if (!isValid()) return 0.0;

            // Calculate total interruption time
            std::chrono::milliseconds total_interruption_time{0};
            for (const auto& interruption : interruptions) {
                total_interruption_time += interruption.duration;
            }

            // Time in bed
            auto time_in_bed = wake_time.value() - bedtime.value();
            auto time_in_bed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_in_bed);

            // Actual sleep time
            auto actual_sleep_ms = time_in_bed_ms - total_interruption_time;

            // Calculate efficiency
            if (time_in_bed_ms.count() <= 0) return 0.0;
            return static_cast<double>(actual_sleep_ms.count()) / time_in_bed_ms.count();
        }
    };

/**
 * @brief User preferences for personalized sleep tracking
 *
 * Configurable parameters that improve detection accuracy
 * through personalization and historical pattern matching.
 */
    struct UserPreferences {
        std::chrono::duration<double, std::ratio<3600>> target_sleep_hours{8.0};  ///< Desired sleep duration
        std::chrono::minutes target_bedtime{1410};                               ///< Preferred bedtime (23:30)
        std::chrono::minutes target_wake_time{450};                              ///< Preferred wake time (07:30)
        std::chrono::minutes weekday_bedtime{1410};                              ///< Weekday bedtime
        std::chrono::minutes weekend_bedtime{1440};                              ///< Weekend bedtime (24:00)
        std::chrono::seconds minimum_interaction_gap{14400};                     ///< Min gap to consider sleep (4 hours)
        std::chrono::seconds time_check_threshold{30};                           ///< Max duration for time check
        bool enable_smart_detection{true};                                       ///< Use advanced pattern recognition
        bool track_interruptions{true};                                          ///< Monitor mid-sleep activity
        double confidence_threshold{0.7};                                        ///< Min confidence for auto-detection

        // Default constructor with sensible defaults
        UserPreferences() = default;

        /**
         * @brief Check if preferences are valid and reasonable
         * @return true if all preferences are within acceptable ranges
         * @performance < 50 microseconds
         */
        bool isValid() const noexcept {
            return target_sleep_hours.count() >= 1.0 &&      // At least 1 hour
                   target_sleep_hours.count() <= 12.0 &&     // Max 12 hours
                   confidence_threshold >= 0.1 &&
                   confidence_threshold <= 1.0 &&
                   minimum_interaction_gap.count() >= 3600;  // At least 1 hour gap
        }

        /**
         * @brief Get bedtime for specific day of week
         * @param day_of_week 0=Sunday, 1=Monday, ..., 6=Saturday
         * @return Appropriate bedtime for that day
         * @performance < 20 microseconds
         */
        std::chrono::minutes getBedtimeForDay(int day_of_week) const noexcept {
            // Weekend: Saturday (6) and Sunday (0)
            return (day_of_week == 0 || day_of_week == 6) ? weekend_bedtime : weekday_bedtime;
        }

        /**
         * @brief Check if interaction gap duration suggests possible sleep
         * @param gap Duration of no meaningful interactions
         * @return true if gap is long enough to consider sleep
         * @performance < 10 microseconds
         */
        bool isLikelySleepGap(std::chrono::milliseconds gap) const noexcept {
            return gap >= minimum_interaction_gap;
        }

        /**
         * @brief Check if interaction duration suggests time check
         * @param duration How long the interaction lasted
         * @return true if duration suggests brief time check
         * @performance < 10 microseconds
         */
        bool isLikelyTimeCheck(std::chrono::milliseconds duration) const noexcept {
            return duration <= time_check_threshold;
        }
    };

/**
 * @brief Time gap structure for gap analysis
 *
 * Represents periods of no meaningful phone interaction,
 * which are candidates for sleep periods.
 */
    struct TimeGap {
        std::chrono::system_clock::time_point start_time;    ///< When gap started
        std::chrono::system_clock::time_point end_time;      ///< When gap ended
        std::chrono::milliseconds duration;                  ///< Total duration of gap
        bool contains_brief_interactions;                     ///< True if had time checks during gap
        int brief_interaction_count;                         ///< Number of brief interactions in gap

        TimeGap() = default;

        TimeGap(
                std::chrono::system_clock::time_point start,
                std::chrono::system_clock::time_point end
        ) noexcept : start_time(start), end_time(end),
                     contains_brief_interactions(false), brief_interaction_count(0) {

            duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        }

        /**
         * @brief Check if this gap is long enough to be considered sleep
         * @param min_duration Minimum duration to consider sleep
         * @return true if gap duration exceeds minimum
         * @performance < 10 microseconds
         */
        bool isLikelySleep(std::chrono::milliseconds min_duration) const noexcept {
            return duration >= min_duration &&
                   brief_interaction_count < 5; // Too many brief interactions suggest not sleeping
        }

        /**
         * @brief Get gap duration in hours
         * @return Duration as double precision hours
         * @performance < 20 microseconds
         */
        double getDurationHours() const noexcept {
            return duration.count() / (1000.0 * 60.0 * 60.0);
        }
    };

// Type aliases for convenience and performance
    using InteractionEventList = std::vector<InteractionEvent>;
    using SleepInterruptionList = std::vector<SleepInterruption>;
    using TimeGapList = std::vector<TimeGap>;

// Performance constants for optimization
    namespace Performance {
        constexpr size_t MAX_EVENTS_CACHE = 10000;           ///< Maximum events to keep in memory
        constexpr size_t DETECTION_BATCH_SIZE = 1000;        ///< Events to process per batch
        constexpr std::chrono::hours DATA_RETENTION_DAYS{30}; ///< How long to keep historical data
        constexpr std::chrono::milliseconds CACHE_TTL{300000}; ///< Cache validity: 5 minutes
    }

} // namespace puuyapu