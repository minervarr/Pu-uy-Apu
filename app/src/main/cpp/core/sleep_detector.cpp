/**
 * @file sleep_detector.cpp
 * @brief Implementation of high-performance sleep detection engine
 *
 * Core C++ implementation optimized for real-time sleep pattern analysis.
 * All algorithms designed for microsecond-level performance on mobile hardware.
 *
 * @author Puñuy Apu Development Team
 * @version 1.0 - Phase 1A: C++ Core
 */

#include "sleep_detector.h"
#include <algorithm>
#include <cmath>
#include <ctime>

namespace puuyapu {

    constexpr const char* LOG_TAG = "PuuyApu_Core";

// ============================================================================
// SleepDetector Implementation
// ============================================================================

    SleepDetector::SleepDetector(const UserPreferences& preferences) {
        MEASURE_PERFORMANCE("SleepDetector::constructor");

        // Initialize with validated preferences
        preferences_storage_ = std::make_unique<UserPreferences>(preferences);
        if (!preferences_storage_->isValid()) {
            SLEEP_LOG_ERROR(LOG_TAG, "Invalid user preferences provided, using defaults");
            preferences_storage_ = std::make_unique<UserPreferences>();
        }

        preferences_.store(preferences_storage_.get());

        // Pre-allocate event storage for performance
        recent_events_.reserve(Performance::MAX_EVENTS_CACHE);

        SLEEP_LOG_INFO(LOG_TAG, "SleepDetector initialized with %d hours target sleep",
                       (int)preferences_storage_->target_sleep_hours.count());
    }

    SleepDetector::~SleepDetector() noexcept {
        SLEEP_LOG_DEBUG(LOG_TAG, "SleepDetector destructor - processed %zu events, detected %zu sleep periods",
                        total_events_processed_.load(), total_sleep_periods_detected_.load());
    }

    void SleepDetector::addInteractionEvent(const InteractionEvent& event) noexcept {
        MEASURE_PERFORMANCE("SleepDetector::addInteractionEvent");

        std::lock_guard<std::mutex> lock(events_mutex_);

        // Add event to circular buffer
        if (recent_events_.size() < Performance::MAX_EVENTS_CACHE) {
            recent_events_.push_back(event);
        } else {
            // Circular buffer is full, overwrite oldest
            recent_events_[event_write_index_] = event;
            event_write_index_ = (event_write_index_ + 1) % Performance::MAX_EVENTS_CACHE;
            event_buffer_full_ = true;
        }

        total_events_processed_++;

        // Invalidate cache when new events arrive
        cached_result_.reset();

        SLEEP_LOG_DEBUG(LOG_TAG, "Added interaction event: type=%d, duration=%ldms",
                        static_cast<int>(event.type), event.duration.count());
    }

    SleepDetectionResult SleepDetector::detectSleepPeriod(
            const std::chrono::system_clock::time_point& current_time) const {

        MEASURE_PERFORMANCE("SleepDetector::detectSleepPeriod");

        // Check if we can use cached result
        if (canUseCachedResult(current_time)) {
            recordPerformanceMetric("cache_hit", std::chrono::microseconds(10));
            return *cached_result_;
        }

        // Get sorted events for analysis
        auto events = getSortedEvents();
        if (events.size() < 2) {
            SLEEP_LOG_DEBUG(LOG_TAG, "Insufficient events for sleep detection: %zu", events.size());
            return SleepDetectionResult{};
        }

        SleepDetectionResult result;

        // Step 1: Find potential sleep start time
        auto sleep_start = findSleepStartTime(events, current_time);
        if (!sleep_start.has_value()) {
            SLEEP_LOG_DEBUG(LOG_TAG, "No sleep start time detected");
            return result;
        }

        result.bedtime = sleep_start;

        // Step 2: Find sleep end time
        auto sleep_end = findSleepEndTime(events, *sleep_start, current_time);
        if (!sleep_end.has_value()) {
            // User might still be sleeping
            SLEEP_LOG_DEBUG(LOG_TAG, "Sleep end time not detected - user may still be sleeping");
            return result;
        }

        result.wake_time = sleep_end;

        // Step 3: Calculate duration
        result.duration = std::chrono::duration<double, std::ratio<3600>>(
                calculateDurationHours(*sleep_start, *sleep_end)
        );

        // Step 4: Analyze interruptions
        result.interruptions = analyzeInterruptions(events, *sleep_start, *sleep_end);

        // Step 5: Calculate quality and confidence
        result.quality_score = calculateSleepQuality(result.interruptions,
                                                     std::chrono::duration_cast<std::chrono::milliseconds>(*sleep_end - *sleep_start));
        result.confidence = static_cast<SleepConfidence>(
                std::min(4, static_cast<int>(calculateConfidenceScore(result) * 5))
        );
        result.pattern_match_score = evaluatePatternConsistency(*sleep_start, *sleep_end);

        // Check for manual confirmation
        for (const auto& event : events) {
            if (event.type == InteractionType::SLEEP_CONFIRMATION &&
                event.timestamp >= *sleep_start - std::chrono::minutes(30) &&
                event.timestamp <= *sleep_start + std::chrono::minutes(30)) {
                result.is_manually_confirmed = true;
                result.confidence = SleepConfidence::VERY_HIGH;
                break;
            }
        }

        // Cache the result
        if (result.isValid()) {
            std::lock_guard<std::mutex> lock(events_mutex_);
            cached_result_ = result;
            last_cache_update_ = current_time;
            total_sleep_periods_detected_++;

            SLEEP_LOG_INFO(LOG_TAG, "Sleep period detected: %.1f hours, confidence=%s",
                           result.duration.count(), result.getConfidenceString());
        }

        return result;
    }

    double SleepDetector::calculateConfidenceScore(const SleepDetectionResult& session) const noexcept {
        MEASURE_PERFORMANCE("SleepDetector::calculateConfidenceScore");

        if (!session.isValid()) {
            return 0.0;
        }

        double score = 0.0;

        // Manual confirmation gets highest weight (50%)
        if (session.is_manually_confirmed) {
            score += 0.5;
        }

        // Duration reasonableness (20%)
        auto prefs = preferences_.load();
        double target_hours = prefs->target_sleep_hours.count();
        double actual_hours = session.duration.count();
        double duration_diff = std::abs(actual_hours - target_hours);
        double duration_score = std::max(0.0, 1.0 - (duration_diff / target_hours));
        score += duration_score * 0.2;

        // Pattern consistency (15%)
        score += session.pattern_match_score * 0.15;

        // Sleep quality (10%)
        score += session.quality_score * 0.1;

        // Time of day appropriateness (5%)
        if (session.bedtime.has_value()) {
            if (isNighttime(*session.bedtime)) {
                score += 0.05;
            }
        }

        return std::min(1.0, score);
    }

    void SleepDetector::updateUserPreferences(const UserPreferences& new_preferences) noexcept {
        MEASURE_PERFORMANCE("SleepDetector::updateUserPreferences");

        if (!new_preferences.isValid()) {
            SLEEP_LOG_ERROR(LOG_TAG, "Invalid preferences provided, ignoring update");
            return;
        }

        // Update atomic pointer
        *preferences_storage_ = new_preferences;
        preferences_.store(preferences_storage_.get());

        // Invalidate cache since preferences changed
        cached_result_.reset();

        SLEEP_LOG_INFO(LOG_TAG, "User preferences updated: target sleep %.1f hours",
                       new_preferences.target_sleep_hours.count());
    }

    bool SleepDetector::isCurrentlyAsleep(
            const std::chrono::system_clock::time_point& current_time) const noexcept {

        MEASURE_PERFORMANCE("SleepDetector::isCurrentlyAsleep");

        auto events = getSortedEvents();
        if (events.empty()) {
            return false;
        }

        // Check time since last meaningful interaction
        auto last_meaningful = std::find_if(events.rbegin(), events.rend(),
                                            [](const InteractionEvent& e) { return e.isMeaningfulUse(); });

        if (last_meaningful == events.rend()) {
            return false;
        }

        auto time_since_last = current_time - last_meaningful->timestamp;
        auto prefs = preferences_.load();

        // Simple heuristic: if no meaningful interaction for minimum gap duration
        return time_since_last >= prefs->minimum_interaction_gap;
    }

    std::optional<std::chrono::system_clock::time_point>
    SleepDetector::getEstimatedSleepStart(
            const std::chrono::system_clock::time_point& current_time) const noexcept {

        if (!isCurrentlyAsleep(current_time)) {
            return std::nullopt;
        }

        auto events = getSortedEvents();
        return findSleepStartTime(events, current_time);
    }

    void SleepDetector::clearOldData(
            const std::chrono::system_clock::time_point& cutoff_time) noexcept {

        MEASURE_PERFORMANCE("SleepDetector::clearOldData");

        std::lock_guard<std::mutex> lock(events_mutex_);

        size_t original_size = recent_events_.size();

        // Remove events older than cutoff
        recent_events_.erase(
                std::remove_if(recent_events_.begin(), recent_events_.end(),
                               [cutoff_time](const InteractionEvent& event) {
                                   return event.timestamp < cutoff_time;
                               }),
                recent_events_.end()
        );

        // Reset circular buffer state if needed
        if (recent_events_.size() < Performance::MAX_EVENTS_CACHE) {
            event_buffer_full_ = false;
            event_write_index_ = 0;
        }

        // Invalidate cache
        cached_result_.reset();

        SLEEP_LOG_DEBUG(LOG_TAG, "Cleared old data: %zu -> %zu events",
                        original_size, recent_events_.size());
    }

    std::unordered_map<std::string, std::chrono::microseconds>
    SleepDetector::getPerformanceMetrics() const {

        std::lock_guard<std::mutex> lock(metrics_mutex_);
        return performance_metrics_;
    }

    void SleepDetector::confirmManualSleep(
            const std::chrono::system_clock::time_point& timestamp) noexcept {

        MEASURE_PERFORMANCE("SleepDetector::confirmManualSleep");

        // Create manual confirmation event
        InteractionEvent confirmation_event{
                timestamp,
                std::chrono::milliseconds(0),
                InteractionType::SLEEP_CONFIRMATION,
                AppCategory::SYSTEM
        };

        addInteractionEvent(confirmation_event);

        SLEEP_LOG_INFO(LOG_TAG, "Manual sleep confirmation recorded");
    }

    SleepDetector::Statistics SleepDetector::getStatistics() const noexcept {
        Statistics stats{};
        stats.total_events_processed = total_events_processed_.load();
        stats.total_sleep_periods_detected = total_sleep_periods_detected_.load();

        // Calculate average confidence from recent detections
        // This would require maintaining a history, simplified for now
        stats.average_confidence_score = 0.75; // Placeholder

        // Get average detection time from performance metrics
        auto metrics = getPerformanceMetrics();
        auto it = metrics.find("SleepDetector::detectSleepPeriod");
        stats.average_detection_time = (it != metrics.end()) ? it->second : std::chrono::microseconds(0);

        // Calculate cache hit rate (simplified)
        stats.cache_hit_rate = 0.8; // Placeholder

        // Estimate memory usage
        std::lock_guard<std::mutex> lock(events_mutex_);
        stats.current_memory_usage_bytes = recent_events_.size() * sizeof(InteractionEvent);

        return stats;
    }

    void SleepDetector::optimizeMemory() noexcept {
        MEASURE_PERFORMANCE("SleepDetector::optimizeMemory");

        // Clear old performance metrics
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            performance_metrics_.clear();
        }

        // Clear old events (keep last 7 days)
        auto cutoff_time = std::chrono::system_clock::now() - std::chrono::hours(24 * 7);
        clearOldData(cutoff_time);

        // Shrink vectors to fit
        {
            std::lock_guard<std::mutex> lock(events_mutex_);
            recent_events_.shrink_to_fit();
        }

        SLEEP_LOG_INFO(LOG_TAG, "Memory optimization completed");
    }

// ============================================================================
// Private Helper Methods
// ============================================================================

    std::optional<std::chrono::system_clock::time_point>
    SleepDetector::findSleepStartTime(
            const InteractionEventList& events,
            const std::chrono::system_clock::time_point& current_time) const noexcept {

        if (events.empty()) {
            return std::nullopt;
        }

        auto prefs = preferences_.load();

        // Look for gaps in meaningful interactions
        auto gaps = detectInteractionGaps(events,
                                          std::chrono::duration_cast<std::chrono::milliseconds>(prefs->minimum_interaction_gap));

        // Find the most recent gap that could be sleep
        for (auto it = gaps.rbegin(); it != gaps.rend(); ++it) {
            if (it->isLikelySleep(std::chrono::duration_cast<std::chrono::milliseconds>(
                    prefs->minimum_interaction_gap))) {
                return it->start_time;
            }
        }

        // If no gaps found, check if last meaningful interaction was long enough ago
        auto last_meaningful = std::find_if(events.rbegin(), events.rend(),
                                            [](const InteractionEvent& e) { return e.isMeaningfulUse(); });

        if (last_meaningful != events.rend()) {
            auto time_since = current_time - last_meaningful->timestamp;
            if (time_since >= prefs->minimum_interaction_gap) {
                return last_meaningful->timestamp;
            }
        }

        return std::nullopt;
    }

    std::optional<std::chrono::system_clock::time_point>
    SleepDetector::findSleepEndTime(
            const InteractionEventList& events,
            const std::chrono::system_clock::time_point& sleep_start,
            const std::chrono::system_clock::time_point& current_time) const noexcept {

        // Find first meaningful interaction after sleep start
        auto wake_event = std::find_if(events.begin(), events.end(),
                                       [sleep_start](const InteractionEvent& e) {
                                           return e.timestamp > sleep_start && e.isMeaningfulUse();
                                       });

        if (wake_event != events.end()) {
            return wake_event->timestamp;
        }

        // If no wake event found, user might still be sleeping
        return std::nullopt;
    }

    SleepInterruptionList SleepDetector::analyzeInterruptions(
            const InteractionEventList& events,
            const std::chrono::system_clock::time_point& sleep_start,
            const std::chrono::system_clock::time_point& sleep_end) const noexcept {

        SleepInterruptionList interruptions;

        // Find all interactions between sleep start and end
        for (const auto& event : events) {
            if (event.timestamp > sleep_start && event.timestamp < sleep_end) {
                // Check if this is a brief interruption vs sleep end
                if (event.isTimeCheck() || event.duration < std::chrono::seconds(120)) {
                    SleepInterruption interruption{
                            event.timestamp,
                            event.duration,
                            event.type,
                            event.category
                    };
                    interruptions.push_back(interruption);
                }
            }
        }

        return interruptions;
    }

    bool SleepDetector::canUseCachedResult(
            const std::chrono::system_clock::time_point& current_time) const noexcept {

        if (!cached_result_.has_value()) {
            return false;
        }

        auto time_since_cache = current_time - last_cache_update_;
        return time_since_cache < CACHE_VALIDITY_DURATION;
    }

    double SleepDetector::evaluatePatternConsistency(
            const std::chrono::system_clock::time_point& sleep_start,
            const std::chrono::system_clock::time_point& sleep_end) const noexcept {

        auto prefs = preferences_.load();

        // Get current day of week for bedtime comparison
        auto time_t = std::chrono::system_clock::to_time_t(sleep_start);
        auto tm = *std::localtime(&time_t);
        auto expected_bedtime = prefs->getBedtimeForDay(tm.tm_wday);

        // Calculate actual bedtime in minutes since midnight
        auto actual_bedtime = getMinutesSinceMidnight(sleep_start);

        // Calculate deviation from expected bedtime
        auto bedtime_diff = std::abs((actual_bedtime - expected_bedtime).count());
        double bedtime_score = std::max(0.0, 1.0 - (bedtime_diff / 180.0)); // 3 hour tolerance

        // Calculate duration consistency
        double actual_duration = calculateDurationHours(sleep_start, sleep_end);
        double target_duration = prefs->target_sleep_hours.count();
        double duration_diff = std::abs(actual_duration - target_duration);
        double duration_score = std::max(0.0, 1.0 - (duration_diff / target_duration));

        // Combined score
        return (bedtime_score + duration_score) / 2.0;
    }

    void SleepDetector::recordPerformanceMetric(
            const std::string& operation,
            std::chrono::microseconds duration) const noexcept {

        std::lock_guard<std::mutex> lock(metrics_mutex_);

        // Update rolling average
        auto it = performance_metrics_.find(operation);
        if (it != performance_metrics_.end()) {
            // Simple rolling average
            it->second = std::chrono::microseconds((it->second.count() + duration.count()) / 2);
        } else {
            performance_metrics_[operation] = duration;
        }
    }

    InteractionEventList SleepDetector::getSortedEvents() const {
        std::lock_guard<std::mutex> lock(events_mutex_);

        InteractionEventList sorted_events = recent_events_;
        std::sort(sorted_events.begin(), sorted_events.end());

        return sorted_events;
    }

    TimeGapList SleepDetector::detectInteractionGaps(
            const InteractionEventList& events,
            std::chrono::milliseconds min_gap_duration) const noexcept {

        TimeGapList gaps;

        if (events.size() < 2) {
            return gaps;
        }

        // Find gaps between consecutive meaningful interactions
        auto prev_meaningful = events.begin();
        for (auto it = events.begin() + 1; it != events.end(); ++it) {
            if (it->isMeaningfulUse()) {
                auto gap_duration = it->timestamp - prev_meaningful->timestamp;

                if (gap_duration >= min_gap_duration) {
                    TimeGap gap(prev_meaningful->timestamp, it->timestamp);

                    // Count brief interactions in gap
                    for (auto gap_it = prev_meaningful + 1; gap_it != it; ++gap_it) {
                        if (gap_it->isTimeCheck()) {
                            gap.contains_brief_interactions = true;
                            gap.brief_interaction_count++;
                        }
                    }

                    gaps.push_back(gap);
                }

                prev_meaningful = it;
            }
        }

        return gaps;
    }

    double SleepDetector::calculateSleepQuality(
            const SleepInterruptionList& interruptions,
            std::chrono::milliseconds total_sleep_duration) const noexcept {

        if (total_sleep_duration.count() <= 0) {
            return 0.0;
        }

        // Base quality score
        double quality = 1.0;

        // Penalize for interruptions
        for (const auto& interruption : interruptions) {
            quality -= interruption.impact_score * 0.1; // Each interruption reduces quality
        }

        // Penalize for too many interruptions
        if (interruptions.size() > 3) {
            quality -= (interruptions.size() - 3) * 0.05;
        }

        return std::max(0.0, std::min(1.0, quality));
    }

// ============================================================================
// Utility Functions
// ============================================================================

    bool isWithinDailyTimeRange(
            const std::chrono::system_clock::time_point& time_point,
            std::chrono::minutes range_start,
            std::chrono::minutes range_end) noexcept {

        auto minutes = getMinutesSinceMidnight(time_point);

        if (range_start <= range_end) {
            // Normal range (e.g., 9:00 to 17:00)
            return minutes >= range_start && minutes <= range_end;
        } else {
            // Overnight range (e.g., 22:00 to 06:00)
            return minutes >= range_start || minutes <= range_end;
        }
    }

    std::chrono::minutes getMinutesSinceMidnight(
            const std::chrono::system_clock::time_point& time_point) noexcept {

        auto time_t = std::chrono::system_clock::to_time_t(time_point);
        auto tm = *std::localtime(&time_t);

        return std::chrono::minutes(tm.tm_hour * 60 + tm.tm_min);
    }

    bool isNighttime(const std::chrono::system_clock::time_point& time_point) noexcept {
        // Define nighttime as 22:00 to 06:00
        return isWithinDailyTimeRange(time_point,
                                      std::chrono::minutes(22 * 60), // 22:00
                                      std::chrono::minutes(6 * 60)   // 06:00
        );
    }

// ============================================================================
// InteractionAnalyzer Implementation
// ============================================================================

    InteractionType InteractionAnalyzer::classifyInteraction(
            const InteractionEvent& event,
            const InteractionEventList& recent_history) noexcept {

        // If already classified, return as-is
        if (event.type != InteractionType::UNKNOWN) {
            return event.type;
        }

        // Classify based on duration
        if (event.duration < std::chrono::seconds(15)) {
            return InteractionType::TIME_CHECK;
        } else if (event.duration < std::chrono::seconds(30)) {
            // Check context - if user just had long session, this might be continuation
            if (!recent_history.empty()) {
                auto last_event = recent_history.back();
                auto time_gap = event.timestamp - last_event.timestamp;

                if (time_gap < std::chrono::minutes(2) &&
                    last_event.type == InteractionType::MEANINGFUL_USE) {
                    return InteractionType::MEANINGFUL_USE;
                }
            }
            return InteractionType::TIME_CHECK;
        } else if (event.duration < std::chrono::minutes(5)) {
            return InteractionType::MEANINGFUL_USE;
        } else {
            return InteractionType::EXTENDED_USE;
        }
    }

    TimeGapList InteractionAnalyzer::detectInteractionGaps(
            const InteractionEventList& events,
            std::chrono::milliseconds minimum_gap) noexcept {

        TimeGapList gaps;

        if (events.size() < 2) {
            return gaps;
        }

        // Events should already be sorted, but ensure it
        auto sorted_events = events;
        std::sort(sorted_events.begin(), sorted_events.end());

        for (size_t i = 1; i < sorted_events.size(); ++i) {
            auto gap_duration = sorted_events[i].timestamp - sorted_events[i-1].timestamp;

            if (gap_duration >= minimum_gap) {
                TimeGap gap(sorted_events[i-1].timestamp, sorted_events[i].timestamp);
                gaps.push_back(gap);
            }
        }

        return gaps;
    }

    bool InteractionAnalyzer::detectSleepPattern(
            const InteractionEventList& events,
            const UserPreferences& preferences) noexcept {

        if (events.empty()) {
            return false;
        }

        // Find last meaningful interaction
        auto last_meaningful = std::find_if(events.rbegin(), events.rend(),
                                            [](const InteractionEvent& e) { return e.isMeaningfulUse(); });

        if (last_meaningful == events.rend()) {
            return false;
        }

        // Check if enough time has passed since last meaningful interaction
        auto now = std::chrono::system_clock::now();
        auto time_since_last = now - last_meaningful->timestamp;

        return time_since_last >= preferences.minimum_interaction_gap;
    }

    bool InteractionAnalyzer::isTimeCheck(const InteractionEvent& event) noexcept {
        return event.isTimeCheck();
    }

    bool InteractionAnalyzer::isMeaningfulUsage(const InteractionEvent& event) noexcept {
        return event.isMeaningfulUse();
    }

} // namespace puuyapu