// core/sleep_detector.cpp - High-performance sleep detection algorithms
#include "../include/sleep_detector.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <unordered_map>
#include <android/log.h>

#define LOG_TAG "PuuyApu_SleepDetector"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace puuyapu {

/**
 * Sleep detection result structure
 * Contains all data from sleep analysis
 */
    struct SleepDetectionResult {
        bool sleep_detected;
        std::chrono::system_clock::time_point bedtime;
        std::chrono::system_clock::time_point wake_time;
        std::chrono::minutes duration;
        SleepConfidence confidence;
        std::vector<SleepInterruption> interruptions;
        double quality_score;
        bool is_ongoing;  // True if sleep is currently happening

        SleepDetectionResult()
                : sleep_detected(false), confidence(SleepConfidence::VERY_LOW),
                  quality_score(0.0), is_ongoing(false) {}
    };

/**
 * Time gap analysis structure
 * Used for detecting potential sleep periods
 */
    struct TimeGap {
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
        std::chrono::milliseconds duration;
        size_t interaction_count_before;
        size_t interaction_count_after;
        bool has_interruptions;

        TimeGap(std::chrono::system_clock::time_point start,
                std::chrono::system_clock::time_point end)
                : start_time(start), end_time(end),
                  duration(std::chrono::duration_cast<std::chrono::milliseconds>(end - start)),
                  interaction_count_before(0), interaction_count_after(0),
                  has_interruptions(false) {}
    };

/**
 * High-performance sleep detector implementation
 * Optimized for real-time processing on mobile devices
 */
    class SleepDetector {
    private:
        UserPreferences preferences_;
        std::vector<SleepSession> historical_sessions_;
        std::unordered_map<std::string, double> pattern_weights_;

        // Performance optimization: pre-allocated vectors
        mutable std::vector<InteractionEvent> event_buffer_;
        mutable std::vector<TimeGap> gap_buffer_;
        mutable std::vector<SleepInterruption> interruption_buffer_;

        // Caching for performance
        mutable std::chrono::system_clock::time_point last_calculation_time_;
        mutable SleepDetectionResult cached_result_;
        mutable bool cache_valid_;

    public:
        /**
         * Constructor with user preferences
         */
        explicit SleepDetector(const UserPreferences& prefs = UserPreferences())
                : preferences_(prefs), cache_valid_(false) {

            // Initialize pattern weights for confidence calculation
            initializePatternWeights();

            // Pre-allocate vectors for performance
            event_buffer_.reserve(1000);
            gap_buffer_.reserve(50);
            interruption_buffer_.reserve(20);

            LOGD("SleepDetector initialized with target sleep: %ld minutes",
                 preferences_.target_sleep_duration.count());
        }

        /**
         * Main sleep detection algorithm
         * Analyzes interaction patterns to detect sleep periods
         *
         * @param events Vector of interaction events (must be sorted by timestamp)
         * @param current_time Current system time for real-time analysis
         * @return Complete sleep detection result
         */
        SleepDetectionResult detectSleepPeriod(
                const std::vector<InteractionEvent>& events,
                const std::chrono::system_clock::time_point& current_time) const {

            // Performance optimization: check cache validity
            if (cache_valid_ &&
                std::chrono::duration_cast<std::chrono::minutes>(
                        current_time - last_calculation_time_).count() < 5) {
                return cached_result_;
            }

            LOGD("Starting sleep detection for %zu events", events.size());

            SleepDetectionResult result;

            if (events.empty()) {
                return result;
            }

            // Step 1: Find significant interaction gaps
            auto gaps = findInteractionGaps(events, current_time);

            if (gaps.empty()) {
                LOGD("No significant gaps found");
                return result;
            }

            // Step 2: Analyze each gap for sleep likelihood
            auto best_sleep_gap = findBestSleepCandidate(gaps, events);

            if (!best_sleep_gap.has_value()) {
                LOGD("No valid sleep candidate found");
                return result;
            }

            // Step 3: Analyze interruptions during sleep period
            auto interruptions = analyzeInterruptions(
                    events, best_sleep_gap->start_time, best_sleep_gap->end_time);

            // Step 4: Calculate confidence score
            double confidence_score = calculateConfidenceScore(
                    *best_sleep_gap, interruptions, events);

            // Step 5: Build result
            result.sleep_detected = confidence_score >= preferences_.confidence_threshold;
            result.bedtime = best_sleep_gap->start_time;
            result.wake_time = best_sleep_gap->end_time;
            result.duration = std::chrono::duration_cast<std::chrono::minutes>(
                    best_sleep_gap->duration);
            result.confidence = scoreToConfidenceLevel(confidence_score);
            result.interruptions = interruptions;
            result.quality_score = calculateSleepQuality(*best_sleep_gap, interruptions);
            result.is_ongoing = isCurrentlyAsleep(events, current_time);

            // Cache result for performance
            cached_result_ = result;
            last_calculation_time_ = current_time;
            cache_valid_ = true;

            LOGI("Sleep detection complete: %s, confidence: %.2f, duration: %ld min",
                 result.sleep_detected ? "YES" : "NO",
                 confidence_score, result.duration.count());

            return result;
        }

        /**
         * Real-time check if user is currently asleep
         * Optimized for frequent calls
         */
        bool isCurrentlyAsleep(
                const std::vector<InteractionEvent>& events,
                const std::chrono::system_clock::time_point& current_time) const {

            if (events.empty()) return false;

            // Check time since last meaningful interaction
            auto last_meaningful = findLastMeaningfulInteraction(events);
            if (!last_meaningful.has_value()) return false;

            auto time_since_last = current_time - last_meaningful->timestamp;
            auto gap_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    time_since_last);

            // Quick check: if gap is shorter than minimum, definitely not asleep
            if (gap_duration < preferences_.minimum_interaction_gap) {
                return false;
            }

            // Advanced check: analyze recent pattern
            return analyzeRecentPatternForSleep(events, current_time);
        }

        /**
         * Update user preferences for personalized detection
         */
        void updatePreferences(const UserPreferences& new_preferences) {
            preferences_ = new_preferences;
            cache_valid_ = false; // Invalidate cache

            LOGD("Preferences updated - target sleep: %ld minutes",
                 preferences_.target_sleep_duration.count());
        }

        /**
         * Add historical sleep session for pattern learning
         */
        void addHistoricalSession(const SleepSession& session) {
            if (session.isValid()) {
                historical_sessions_.push_back(session);

                // Keep only recent sessions for performance (last 30 days)
                if (historical_sessions_.size() > 30) {
                    historical_sessions_.erase(historical_sessions_.begin());
                }

                cache_valid_ = false; // Patterns changed, invalidate cache
            }
        }

    private:
        /**
         * Initialize pattern weights for confidence calculation
         */
        void initializePatternWeights() {
            pattern_weights_["duration_match"] = 0.25;      // How well duration matches target
            pattern_weights_["timing_consistency"] = 0.20;   // Consistency with historical timing
            pattern_weights_["gap_quality"] = 0.20;         // Quality of interaction gap
            pattern_weights_["interruption_penalty"] = 0.15; // Penalty for many interruptions
            pattern_weights_["manual_confirmation"] = 0.20;  // Bonus for manual confirmation
        }

        /**
         * Find significant gaps in interaction timeline
         * Returns gaps that could potentially be sleep periods
         */
        std::vector<TimeGap> findInteractionGaps(
                const std::vector<InteractionEvent>& events,
                const std::chrono::system_clock::time_point& current_time) const {

            gap_buffer_.clear();

            if (events.size() < 2) return gap_buffer_;

            // Find gaps between consecutive meaningful interactions
            auto prev_meaningful = events.begin();

            for (auto it = events.begin() + 1; it != events.end(); ++it) {
                // Skip time checks and very brief interactions
                if (!it->isMeaningfulUse()) continue;

                auto gap_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        it->timestamp - prev_meaningful->timestamp);

                // Check if gap is significant enough to be sleep
                if (gap_duration >= preferences_.minimum_interaction_gap) {
                    TimeGap gap(prev_meaningful->timestamp, it->timestamp);
                    gap_buffer_.push_back(gap);
                }

                prev_meaningful = it;
            }

            // Check gap from last interaction to current time
            if (!events.empty()) {
                auto last_event = findLastMeaningfulInteraction(events);
                if (last_event.has_value()) {
                    auto final_gap = std::chrono::duration_cast<std::chrono::milliseconds>(
                            current_time - last_event->timestamp);

                    if (final_gap >= preferences_.minimum_interaction_gap) {
                        TimeGap gap(last_event->timestamp, current_time);
                        gap_buffer_.push_back(gap);
                    }
                }
            }

            LOGD("Found %zu potential sleep gaps", gap_buffer_.size());
            return gap_buffer_;
        }

        /**
         * Find the most likely sleep candidate from detected gaps
         */
        std::optional<TimeGap> findBestSleepCandidate(
                const std::vector<TimeGap>& gaps,
                const std::vector<InteractionEvent>& events) const {

            if (gaps.empty()) return std::nullopt;

            double best_score = 0.0;
            std::optional<TimeGap> best_candidate;

            for (const auto& gap : gaps) {
                double score = evaluateGapAsSleep(gap, events);

                if (score > best_score) {
                    best_score = score;
                    best_candidate = gap;
                }
            }

            LOGD("Best sleep candidate score: %.2f", best_score);
            return best_candidate;
        }

        /**
         * Evaluate how likely a gap is to represent actual sleep
         */
        double evaluateGapAsSleep(const TimeGap& gap,
                                  const std::vector<InteractionEvent>& events) const {
            double score = 0.0;

            // Factor 1: Duration appropriateness (prefer 4-12 hour gaps)
            auto hours = std::chrono::duration_cast<std::chrono::hours>(gap.duration).count();
            if (hours >= 4 && hours <= 12) {
                score += 0.3;
                // Bonus for duration close to target
                auto target_hours = preferences_.target_sleep_duration.count() / 60.0;
                double duration_match = 1.0 - std::abs(hours - target_hours) / target_hours;
                score += duration_match * 0.2;
            } else if (hours < 4) {
                score -= 0.2; // Penalty for too short
            }

            // Factor 2: Timing consistency with historical patterns
            score += evaluateTimingConsistency(gap);

            // Factor 3: Activity patterns before/after gap
            score += evaluateActivityPatterns(gap, events);

            // Factor 4: Day of week considerations
            score += evaluateDayOfWeekPattern(gap);

            return std::max(0.0, std::min(1.0, score));
        }

        /**
         * Analyze interruptions during a potential sleep period
         */
        std::vector<SleepInterruption> analyzeInterruptions(
                const std::vector<InteractionEvent>& events,
                const std::chrono::system_clock::time_point& sleep_start,
                const std::chrono::system_clock::time_point& sleep_end) const {

            interruption_buffer_.clear();

            // Find events that occurred during the sleep period
            for (const auto& event : events) {
                if (event.timestamp > sleep_start && event.timestamp < sleep_end) {
                    SleepInterruption interruption(
                            event.timestamp, event.duration, event.type, event.category);
                    interruption_buffer_.push_back(interruption);
                }
            }

            LOGD("Found %zu interruptions during sleep period", interruption_buffer_.size());
            return interruption_buffer_;
        }

        /**
         * Calculate confidence score for detected sleep period
         */
        double calculateConfidenceScore(
                const TimeGap& sleep_gap,
                const std::vector<SleepInterruption>& interruptions,
                const std::vector<InteractionEvent>& events) const {

            double score = 0.0;

            // Base score from gap evaluation
            score += evaluateGapAsSleep(sleep_gap, events) * 0.4;

            // Pattern matching with historical data
            score += evaluateHistoricalPatternMatch(sleep_gap) * 0.3;

            // Interruption analysis
            score += evaluateInterruptionPattern(interruptions) * 0.2;

            // Manual confirmation bonus
            if (hasManualConfirmation(events, sleep_gap)) {
                score += 0.1;
            }

            return std::max(0.0, std::min(1.0, score));
        }

        /**
         * Calculate sleep quality score based on duration and interruptions
         */
        double calculateSleepQuality(
                const TimeGap& sleep_gap,
                const std::vector<SleepInterruption>& interruptions) const {

            double quality = 1.0; // Start with perfect quality

            // Duration factor
            auto hours = std::chrono::duration_cast<std::chrono::hours>(
                    sleep_gap.duration).count();
            auto target_hours = preferences_.target_sleep_duration.count() / 60.0;

            if (hours < target_hours * 0.8) {
                quality -= 0.2; // Penalty for too little sleep
            } else if (hours > target_hours * 1.3) {
                quality -= 0.1; // Small penalty for too much sleep
            }

            // Interruption factor
            size_t brief_interruptions = 0;
            size_t long_interruptions = 0;

            for (const auto& interruption : interruptions) {
                if (interruption.is_brief_check) {
                    brief_interruptions++;
                } else {
                    long_interruptions++;
                }
            }

            // Penalty for interruptions
            quality -= brief_interruptions * 0.05;   // 5% per brief interruption
            quality -= long_interruptions * 0.15;    // 15% per long interruption

            return std::max(0.0, std::min(1.0, quality));
        }

        /**
         * Helper functions for pattern analysis
         */
        std::optional<InteractionEvent> findLastMeaningfulInteraction(
                const std::vector<InteractionEvent>& events) const {

            for (auto it = events.rbegin(); it != events.rend(); ++it) {
                if (it->isMeaningfulUse()) {
                    return *it;
                }
            }
            return std::nullopt;
        }

        double evaluateTimingConsistency(const TimeGap& gap) const {
            // Compare gap timing with historical sleep patterns
            // This is a simplified version - could be enhanced with ML

            auto gap_start_hour = std::chrono::duration_cast<std::chrono::hours>(
                    gap.start_time.time_since_epoch()).count() % 24;

            // Prefer gaps that start during typical bedtime hours (20:00-02:00)
            if ((gap_start_hour >= 20 && gap_start_hour <= 23) ||
                (gap_start_hour >= 0 && gap_start_hour <= 2)) {
                return 0.2;
            } else if (gap_start_hour >= 3 && gap_start_hour <= 6) {
                return 0.1; // Some people go to bed very late
            }

            return 0.0;
        }

        double evaluateActivityPatterns(const TimeGap& gap,
                                        const std::vector<InteractionEvent>& events) const {
            // Analyze activity before and after the gap
            // High activity before bedtime and after wake time is good

            double score = 0.0;

            // Count interactions in the hour before gap
            auto hour_before = gap.start_time - std::chrono::hours(1);
            size_t pre_sleep_activity = 0;

            for (const auto& event : events) {
                if (event.timestamp >= hour_before &&
                    event.timestamp <= gap.start_time &&
                    event.isMeaningfulUse()) {
                    pre_sleep_activity++;
                }
            }

            // Moderate activity before sleep is good (not too much, not too little)
            if (pre_sleep_activity >= 2 && pre_sleep_activity <= 8) {
                score += 0.1;
            }

            return score;
        }

        double evaluateDayOfWeekPattern(const TimeGap& gap) const {
            // Different patterns for weekdays vs weekends
            auto time_t = std::chrono::system_clock::to_time_t(gap.start_time);
            auto tm = *std::localtime(&time_t);

            // Weekend nights might have later bedtimes
            if (tm.tm_wday == 5 || tm.tm_wday == 6) { // Friday or Saturday
                return 0.05; // Small bonus for weekend flexibility
            }

            return 0.0;
        }

        double evaluateHistoricalPatternMatch(const TimeGap& gap) const {
            if (historical_sessions_.empty()) return 0.0;

            // Compare with recent historical patterns
            double similarity_sum = 0.0;
            size_t valid_comparisons = 0;

            for (const auto& session : historical_sessions_) {
                if (session.confidence == SleepConfidence::HIGH ||
                    session.confidence == SleepConfidence::VERY_HIGH) {

                    auto historical_duration = session.actual_sleep_duration;
                    auto gap_duration = std::chrono::duration_cast<std::chrono::minutes>(
                            gap.duration);

                    // Calculate similarity (1.0 = identical, 0.0 = very different)
                    double duration_diff = std::abs(
                            historical_duration.count() - gap_duration.count());
                    double similarity = std::max(0.0,
                                                 1.0 - (duration_diff / historical_duration.count()));

                    similarity_sum += similarity;
                    valid_comparisons++;
                }
            }

            if (valid_comparisons > 0) {
                return (similarity_sum / valid_comparisons) * 0.2;
            }

            return 0.0;
        }

        double evaluateInterruptionPattern(
                const std::vector<SleepInterruption>& interruptions) const {

            if (interruptions.empty()) {
                return 0.2; // Bonus for uninterrupted sleep
            }

            double score = 0.1; // Base score for some interruptions

            // Count brief vs long interruptions
            size_t brief_count = 0;
            for (const auto& interruption : interruptions) {
                if (interruption.is_brief_check) {
                    brief_count++;
                }
            }

            // Brief interruptions are more acceptable
            double brief_ratio = static_cast<double>(brief_count) / interruptions.size();
            score += brief_ratio * 0.1;

            // Penalty for too many interruptions
            if (interruptions.size() > 5) {
                score -= 0.1;
            }

            return std::max(0.0, score);
        }

        bool hasManualConfirmation(const std::vector<InteractionEvent>& events,
                                   const TimeGap& gap) const {

            // Look for sleep confirmation event near the gap start
            auto search_window = std::chrono::minutes(30);

            for (const auto& event : events) {
                if (event.type == InteractionType::SLEEP_CONFIRMATION &&
                    event.timestamp >= (gap.start_time - search_window) &&
                    event.timestamp <= (gap.start_time + search_window)) {
                    return true;
                }
            }

            return false;
        }

        bool analyzeRecentPatternForSleep(
                const std::vector<InteractionEvent>& events,
                const std::chrono::system_clock::time_point& current_time) const {

            // Advanced pattern analysis for real-time sleep detection
            // This could be enhanced with ML models in Phase 4

            auto last_hour = current_time - std::chrono::hours(1);
            size_t recent_activity = 0;

            for (const auto& event : events) {
                if (event.timestamp >= last_hour && event.isMeaningfulUse()) {
                    recent_activity++;
                }
            }

            // If very low activity in the last hour, likely asleep
            return recent_activity <= 1;
        }

        SleepConfidence scoreToConfidenceLevel(double score) const {
            if (score >= 0.9) return SleepConfidence::VERY_HIGH;
            if (score >= 0.75) return SleepConfidence::HIGH;
            if (score >= 0.5) return SleepConfidence::MEDIUM;
            if (score >= 0.3) return SleepConfidence::LOW;
            return SleepConfidence::VERY_LOW;
        }
    };

} // namespace puuyapu