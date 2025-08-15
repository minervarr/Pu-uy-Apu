// models/interaction_event.cpp - Optimized interaction event model
#include <chrono>
#include <string>
#include <vector>
#include <android/log.h>

#define LOG_TAG "PuuyApu_Native"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace puuyapu {

/**
 * Enumeration of app interaction types for sleep detection
 * Optimized for fast comparison and minimal memory usage
 */
    enum class InteractionType : uint8_t {
        UNKNOWN = 0,
        TIME_CHECK = 1,          // Quick time/notification check < 30 seconds
        MEANINGFUL_USE = 2,       // Actual app usage > 30 seconds
        NOTIFICATION_RESPONSE = 3, // Responding to notifications
        EXTENDED_USE = 4,         // Long session > 5 minutes
        SLEEP_CONFIRMATION = 5    // Manual "Going to Sleep" button
    };

/**
 * Enumeration of app categories for context analysis
 * Used to improve sleep detection accuracy
 */
    enum class AppCategory : uint8_t {
        UNKNOWN = 0,
        SOCIAL_MEDIA = 1,    // Facebook, Instagram, Twitter, etc.
        MESSAGING = 2,       // WhatsApp, Telegram, SMS, etc.
        ENTERTAINMENT = 3,   // YouTube, Netflix, games, etc.
        PRODUCTIVITY = 4,    // Email, calendar, notes, etc.
        CLOCK_ALARM = 5,     // Clock, alarm, weather apps
        SYSTEM = 6           // Settings, system apps
    };

/**
 * High-performance interaction event structure
 * Designed for minimal memory footprint and fast processing
 * Total size: 32 bytes (cache-friendly)
 */
    struct InteractionEvent {
        std::chrono::system_clock::time_point timestamp;  // 8 bytes
        std::chrono::milliseconds duration;                // 8 bytes
        InteractionType type;                              // 1 byte
        AppCategory category;                              // 1 byte
        uint16_t app_hash;                                // 2 bytes (hash of package name)
        uint32_t session_id;                              // 4 bytes
        uint64_t user_interaction_count;                  // 8 bytes

        // Constructors for performance
        InteractionEvent() = default;

        InteractionEvent(
                std::chrono::system_clock::time_point ts,
                std::chrono::milliseconds dur,
                InteractionType t,
                AppCategory cat = AppCategory::UNKNOWN
        ) : timestamp(ts), duration(dur), type(t), category(cat),
            app_hash(0), session_id(0), user_interaction_count(0) {}

        // Fast comparison operators for sorting and searching
        bool operator<(const InteractionEvent& other) const {
            return timestamp < other.timestamp;
        }

        bool operator==(const InteractionEvent& other) const {
            return timestamp == other.timestamp &&
                   duration == other.duration &&
                   type == other.type;
        }

        // Utility methods for sleep detection
        bool isTimeCheck() const {
            return type == InteractionType::TIME_CHECK ||
                   (category == AppCategory::CLOCK_ALARM && duration.count() < 30000);
        }

        bool isMeaningfulUse() const {
            return type == InteractionType::MEANINGFUL_USE ||
                   type == InteractionType::EXTENDED_USE ||
                   duration.count() >= 30000;
        }

        bool isSleepRelated() const {
            return type == InteractionType::SLEEP_CONFIRMATION ||
                   (category == AppCategory::CLOCK_ALARM && duration.count() < 10000);
        }

        // Performance-optimized serialization for JNI
        void serialize(uint8_t* buffer) const {
            // Efficient binary serialization for Java transfer
            auto* ptr = reinterpret_cast<uint64_t*>(buffer);
            *ptr++ = static_cast<uint64_t>(timestamp.time_since_epoch().count());
            *ptr++ = static_cast<uint64_t>(duration.count());

            auto* byte_ptr = reinterpret_cast<uint8_t*>(ptr);
            *byte_ptr++ = static_cast<uint8_t>(type);
            *byte_ptr++ = static_cast<uint8_t>(category);

            auto* short_ptr = reinterpret_cast<uint16_t*>(byte_ptr);
            *short_ptr++ = app_hash;

            auto* int_ptr = reinterpret_cast<uint32_t*>(short_ptr);
            *int_ptr++ = session_id;

            auto* long_ptr = reinterpret_cast<uint64_t*>(int_ptr);
            *long_ptr = user_interaction_count;
        }

        static InteractionEvent deserialize(const uint8_t* buffer) {
            InteractionEvent event;
            const auto* ptr = reinterpret_cast<const uint64_t*>(buffer);

            event.timestamp = std::chrono::system_clock::time_point(
                    std::chrono::nanoseconds(*ptr++)
            );
            event.duration = std::chrono::milliseconds(*ptr++);

            const auto* byte_ptr = reinterpret_cast<const uint8_t*>(ptr);
            event.type = static_cast<InteractionType>(*byte_ptr++);
            event.category = static_cast<AppCategory>(*byte_ptr++);

            const auto* short_ptr = reinterpret_cast<const uint16_t*>(byte_ptr);
            event.app_hash = *short_ptr++;

            const auto* int_ptr = reinterpret_cast<const uint32_t*>(short_ptr);
            event.session_id = *int_ptr++;

            const auto* long_ptr = reinterpret_cast<const uint64_t*>(int_ptr);
            event.user_interaction_count = *long_ptr;

            return event;
        }
    };

/**
 * Sleep session confidence levels
 * Used for accuracy reporting and validation
 */
    enum class SleepConfidence : uint8_t {
        VERY_LOW = 0,    // < 30% confidence, likely false positive
        LOW = 1,         // 30-50% confidence, uncertain detection
        MEDIUM = 2,      // 50-75% confidence, probable sleep
        HIGH = 3,        // 75-90% confidence, very likely sleep
        VERY_HIGH = 4    // 90%+ confidence, manual confirmation or strong pattern
    };

/**
 * Sleep interruption data structure
 * Tracks wake-ups during sleep period for quality analysis
 */
    struct SleepInterruption {
        std::chrono::system_clock::time_point timestamp;
        std::chrono::milliseconds duration;
        InteractionType cause;
        AppCategory app_category;
        bool is_brief_check;  // < 30 seconds, likely time check

        SleepInterruption() = default;

        SleepInterruption(
                std::chrono::system_clock::time_point ts,
                std::chrono::milliseconds dur,
                InteractionType c,
                AppCategory cat = AppCategory::UNKNOWN
        ) : timestamp(ts), duration(dur), cause(c), app_category(cat),
            is_brief_check(dur.count() < 30000) {}
    };

/**
 * Complete sleep session data structure
 * Optimized for both storage and real-time analysis
 */
    struct SleepSession {
        uint64_t session_id;
        std::chrono::system_clock::time_point bedtime;
        std::chrono::system_clock::time_point wake_time;
        std::chrono::minutes target_sleep_duration;
        std::chrono::minutes actual_sleep_duration;
        SleepConfidence confidence;
        bool manually_confirmed;

        // Sleep quality metrics
        std::vector<SleepInterruption> interruptions;
        uint32_t total_interruptions;
        std::chrono::minutes total_interruption_time;
        double sleep_efficiency;  // Actual sleep / time in bed

        // Pattern analysis data
        bool matches_historical_pattern;
        double pattern_deviation_score;  // 0.0 = perfect match, 1.0 = completely different

        // Constructors
        SleepSession() : session_id(0), confidence(SleepConfidence::LOW),
                         manually_confirmed(false), total_interruptions(0),
                         sleep_efficiency(0.0), matches_historical_pattern(false),
                         pattern_deviation_score(1.0) {}

        SleepSession(
                std::chrono::system_clock::time_point bedtime_,
                std::chrono::system_clock::time_point wake_time_,
                bool manual = false
        ) : bedtime(bedtime_), wake_time(wake_time_), manually_confirmed(manual),
            total_interruptions(0), sleep_efficiency(0.0),
            matches_historical_pattern(false), pattern_deviation_score(1.0) {

            // Generate unique session ID based on timestamp
            session_id = static_cast<uint64_t>(bedtime.time_since_epoch().count());

            // Calculate durations
            actual_sleep_duration = std::chrono::duration_cast<std::chrono::minutes>(
                    wake_time - bedtime
            );
            target_sleep_duration = std::chrono::minutes(480); // Default 8 hours

            // Initial confidence based on manual confirmation
            confidence = manual ? SleepConfidence::HIGH : SleepConfidence::MEDIUM;
        }

        // Utility methods
        bool isValid() const {
            return bedtime < wake_time &&
                   actual_sleep_duration.count() >= 60 && // At least 1 hour
                   actual_sleep_duration.count() <= 1440; // Max 24 hours
        }

        void addInterruption(const SleepInterruption& interruption) {
            interruptions.push_back(interruption);
            total_interruptions++;
            total_interruption_time += interruption.duration;

            // Recalculate sleep efficiency
            auto time_in_bed = std::chrono::duration_cast<std::chrono::minutes>(
                    wake_time - bedtime
            );
            auto actual_sleep = time_in_bed - total_interruption_time;
            sleep_efficiency = static_cast<double>(actual_sleep.count()) /
                               time_in_bed.count();
        }

        double getSleepQualityScore() const {
            // Simple quality score based on efficiency and interruptions
            double base_score = sleep_efficiency;
            double interruption_penalty = std::min(0.3, total_interruptions * 0.05);
            return std::max(0.0, base_score - interruption_penalty);
        }

        // Performance-optimized serialization
        static constexpr size_t SERIALIZED_SIZE = 128; // Fixed size for performance

        void serialize(uint8_t* buffer) const {
            auto* ptr = reinterpret_cast<uint64_t*>(buffer);
            *ptr++ = session_id;
            *ptr++ = static_cast<uint64_t>(bedtime.time_since_epoch().count());
            *ptr++ = static_cast<uint64_t>(wake_time.time_since_epoch().count());
            *ptr++ = static_cast<uint64_t>(target_sleep_duration.count());
            *ptr++ = static_cast<uint64_t>(actual_sleep_duration.count());

            auto* byte_ptr = reinterpret_cast<uint8_t*>(ptr);
            *byte_ptr++ = static_cast<uint8_t>(confidence);
            *byte_ptr++ = manually_confirmed ? 1 : 0;

            auto* int_ptr = reinterpret_cast<uint32_t*>(byte_ptr);
            *int_ptr++ = total_interruptions;

            auto* double_ptr = reinterpret_cast<double*>(int_ptr);
            *double_ptr++ = sleep_efficiency;
            *double_ptr = pattern_deviation_score;
        }
    };

/**
 * User preferences for sleep tracking
 * Configurable parameters for personalization
 */
    struct UserPreferences {
        std::chrono::minutes target_sleep_duration;      // Default: 8 hours
        std::chrono::minutes target_bedtime;             // Minutes since midnight
        std::chrono::minutes target_wake_time;           // Minutes since midnight
        std::chrono::minutes weekday_bedtime;            // Separate weekday schedule
        std::chrono::minutes weekend_bedtime;            // Separate weekend schedule
        std::chrono::seconds minimum_interaction_gap;    // Min gap to consider sleep
        std::chrono::seconds time_check_threshold;       // Max duration for time check
        bool enable_smart_detection;                     // Use ML/pattern recognition
        bool track_interruptions;                        // Monitor mid-sleep activity
        double confidence_threshold;                     // Min confidence for auto-detection

        // Default constructor with sensible defaults
        UserPreferences()
                : target_sleep_duration(480)     // 8 hours
                , target_bedtime(1410)           // 23:30 (23*60 + 30)
                , target_wake_time(450)          // 07:30 (7*60 + 30)
                , weekday_bedtime(1410)          // 23:30
                , weekend_bedtime(1440)          // 24:00
                , minimum_interaction_gap(14400) // 4 hours
                , time_check_threshold(30)       // 30 seconds
                , enable_smart_detection(true)
                , track_interruptions(true)
                , confidence_threshold(0.7)      // 70% confidence
        {}

        // Validation methods
        bool isValid() const {
            return target_sleep_duration.count() >= 60 &&    // At least 1 hour
                   target_sleep_duration.count() <= 720 &&   // Max 12 hours
                   confidence_threshold >= 0.1 &&
                   confidence_threshold <= 1.0;
        }

        // Get bedtime for specific day of week
        std::chrono::minutes getBedtimeForDay(int day_of_week) const {
            // day_of_week: 0=Sunday, 1=Monday, ..., 6=Saturday
            return (day_of_week == 0 || day_of_week == 6) ? weekend_bedtime : weekday_bedtime;
        }

        // Check if interaction gap indicates possible sleep
        bool isLikelySleepGap(std::chrono::milliseconds gap) const {
            return gap >= minimum_interaction_gap;
        }

        // Check if interaction duration suggests time check
        bool isLikelyTimeCheck(std::chrono::milliseconds duration) const {
            return duration <= time_check_threshold;
        }
    };

} // namespace puuyapu