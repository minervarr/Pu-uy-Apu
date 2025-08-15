/**
 * @file pattern_matcher.cpp
 * @brief Implementation of sleep pattern recognition
 *
 * Statistical analysis of historical sleep patterns for improved accuracy.
 */

#include "pattern_matcher.h"
#include "time_utils.h"
#include <ctime>
#include <algorithm>
#include <numeric>

namespace puuyapu {

    void PatternMatcher::updatePatterns(const SleepDetectionResult& session) noexcept {
        if (!session.isValid()) {
            return;
        }

        // Get day of week for bedtime
        auto time_t = std::chrono::system_clock::to_time_t(session.bedtime.value());
        auto tm = *std::localtime(&time_t);
        int day_of_week = tm.tm_wday;

        // Update typical bedtime for this day of week
        auto bedtime_minutes = getMinutesSinceMidnight(session.bedtime.value());

        // Use exponential moving average for smooth updates
        const double alpha = 0.1; // Learning rate
        auto& typical_bedtime = typical_bedtimes_[day_of_week];

        if (total_sleep_sessions_ == 0) {
            typical_bedtime = bedtime_minutes;
        } else {
            auto current_minutes = typical_bedtime.count();
            auto new_minutes = bedtime_minutes.count();
            auto updated_minutes = current_minutes * (1.0 - alpha) + new_minutes * alpha;
            typical_bedtime = std::chrono::minutes(static_cast<int>(updated_minutes));
        }

        // Update wake time similarly
        if (session.wake_time.has_value()) {
            auto wake_minutes = getMinutesSinceMidnight(session.wake_time.value());
            auto& typical_wake = typical_wake_times_[day_of_week];

            if (total_sleep_sessions_ == 0) {
                typical_wake = wake_minutes;
            } else {
                auto current_minutes = typical_wake.count();
                auto new_minutes = wake_minutes.count();
                auto updated_minutes = current_minutes * (1.0 - alpha) + new_minutes * alpha;
                typical_wake = std::chrono::minutes(static_cast<int>(updated_minutes));
            }
        }

        // Update average sleep duration
        double current_duration = session.duration.count();
        if (total_sleep_sessions_ == 0) {
            average_sleep_duration_ = std::chrono::duration<double, std::ratio<3600>>(current_duration);
        } else {
            double avg = average_sleep_duration_.count();
            avg = avg * (1.0 - alpha) + current_duration * alpha;
            average_sleep_duration_ = std::chrono::duration<double, std::ratio<3600>>(avg);
        }

        // Update pattern confidence for this day
        if (session.confidence >= SleepConfidence::MEDIUM) {
            pattern_confidence_[day_of_week] = std::min(1.0, pattern_confidence_[day_of_week] + 0.05);
        }

        // Calculate schedule regularity
        if (total_sleep_sessions_ > 7) { // Need at least a week of data
            calculateScheduleRegularity();
        }

        total_sleep_sessions_++;
    }

    double PatternMatcher::calculatePatternMatch(
            std::chrono::system_clock::time_point bedtime,
            std::chrono::system_clock::time_point wake_time) const noexcept {

        // Get day of week
        auto time_t = std::chrono::system_clock::to_time_t(bedtime);
        auto tm = *std::localtime(&time_t);
        int day_of_week = tm.tm_wday;

        double total_score = 0.0;
        int factors = 0;

        // Bedtime consistency score
        auto actual_bedtime = getMinutesSinceMidnight(bedtime);
        auto expected_bedtime = typical_bedtimes_[day_of_week];
        auto bedtime_diff = std::abs((actual_bedtime - expected_bedtime).count());

        // Score based on deviation (within 2 hours = good)
        double bedtime_score = std::max(0.0, 1.0 - (bedtime_diff / 120.0));
        total_score += bedtime_score * pattern_confidence_[day_of_week];
        factors++;

        // Wake time consistency score (if available)
        auto actual_wake = getMinutesSinceMidnight(wake_time);
        auto expected_wake = typical_wake_times_[day_of_week];
        auto wake_diff = std::abs((actual_wake - expected_wake).count());

        double wake_score = std::max(0.0, 1.0 - (wake_diff / 120.0));
        total_score += wake_score * pattern_confidence_[day_of_week];
        factors++;

        // Duration consistency score
        double actual_duration = calculateDurationHours(bedtime, wake_time);
        double expected_duration = average_sleep_duration_.count();
        double duration_diff = std::abs(actual_duration - expected_duration);

        double duration_score = std::max(0.0, 1.0 - (duration_diff / expected_duration));
        total_score += duration_score;
        factors++;

        return factors > 0 ? total_score / factors : 0.0;
    }

    std::chrono::minutes PatternMatcher::getExpectedBedtime(int day_of_week) const noexcept {
        if (day_of_week < 0 || day_of_week > 6) {
            return std::chrono::minutes(1410); // Default 23:30
        }
        return typical_bedtimes_[day_of_week];
    }

    bool PatternMatcher::isLikelySleepTime(
            std::chrono::system_clock::time_point current_time,
            std::chrono::system_clock::time_point last_interaction) const noexcept {

        // Check if enough time has passed since last interaction
        auto time_since_last = current_time - last_interaction;
        if (time_since_last < std::chrono::hours(2)) {
            return false;
        }

        // Check if current time is within typical sleep window
        auto time_t = std::chrono::system_clock::to_time_t(current_time);
        auto tm = *std::localtime(&time_t);
        int day_of_week = tm.tm_wday;

        auto current_minutes = getMinutesSinceMidnight(current_time);
        auto expected_bedtime = typical_bedtimes_[day_of_week];

        // Consider sleep time if within 3 hours of typical bedtime
        auto time_diff = std::abs((current_minutes - expected_bedtime).count());

        return time_diff <= 180 && pattern_confidence_[day_of_week] > 0.3;
    }

    void PatternMatcher::calculateScheduleRegularity() noexcept {
        // Calculate variance in bedtimes across all days
        std::vector<double> bedtime_minutes;
        bedtime_minutes.reserve(7);

        for (const auto& bedtime : typical_bedtimes_) {
            bedtime_minutes.push_back(bedtime.count());
        }

        // Calculate mean
        double mean = std::accumulate(bedtime_minutes.begin(), bedtime_minutes.end(), 0.0) / 7.0;

        // Calculate variance
        double variance = 0.0;
        for (double minutes : bedtime_minutes) {
            variance += (minutes - mean) * (minutes - mean);
        }
        variance /= 7.0;

        // Convert variance to regularity score (lower variance = higher regularity)
        double std_dev = std::sqrt(variance);
        schedule_regularity_score_ = std::max(0.0, 1.0 - (std_dev / 180.0)); // 3 hours max deviation
    }

} // namespace puuyapu
