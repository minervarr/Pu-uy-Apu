/**
 * @file pattern_matcher.h
 * @brief Sleep pattern recognition and historical comparison
 *
 * Analyzes user's historical sleep patterns to improve detection accuracy.
 * Uses statistical analysis to identify typical sleep schedules.
 *
 * @performance Target: < 1ms pattern matching
 */

#pragma once

#include "puuyapu_types.h"
#include <unordered_map>
#include <array>

namespace puuyapu {

    /**
     * @brief Historical sleep pattern analyzer
     *
     * Maintains statistical models of user's sleep patterns to improve
     * detection accuracy through personalization.
     */
    class PatternMatcher {
    private:
        // Weekly pattern storage (day of week -> typical bedtime/wake time)
        std::array<std::chrono::minutes, 7> typical_bedtimes_;
        std::array<std::chrono::minutes, 7> typical_wake_times_;
        std::array<double, 7> pattern_confidence_;

        // Overall statistics
        std::chrono::duration<double, std::ratio<3600>> average_sleep_duration_{8.0};
        double schedule_regularity_score_{0.0};
        size_t total_sleep_sessions_{0};

    public:
        /**
         * @brief Update patterns with new sleep session
         * @param session Completed sleep session to learn from
         * @performance < 200 microseconds
         */
        void updatePatterns(const SleepDetectionResult& session) noexcept;

        /**
         * @brief Calculate how well a sleep session matches historical patterns
         * @param bedtime Detected bedtime
         * @param wake_time Detected wake time
         * @return Pattern match score 0.0-1.0
         * @performance < 300 microseconds
         */
        double calculatePatternMatch(
                std::chrono::system_clock::time_point bedtime,
                std::chrono::system_clock::time_point wake_time) const noexcept;

        /**
         * @brief Get expected bedtime for specific day
         * @param day_of_week 0=Sunday, 1=Monday, ..., 6=Saturday
         * @return Expected bedtime in minutes since midnight
         * @performance < 20 microseconds
         */
        std::chrono::minutes getExpectedBedtime(int day_of_week) const noexcept;

        /**
         * @brief Check if current pattern suggests user is likely sleeping
         * @param current_time Current timestamp
         * @param last_interaction When user last used phone meaningfully
         * @return true if patterns suggest sleep period
         * @performance < 100 microseconds
         */
        bool isLikelySleepTime(
                std::chrono::system_clock::time_point current_time,
                std::chrono::system_clock::time_point last_interaction) const noexcept;

        /**
         * @brief Get schedule regularity score
         * @return 0.0-1.0 indicating how regular sleep schedule is
         * @performance < 10 microseconds
         */
        double getScheduleRegularity() const noexcept { return schedule_regularity_score_; }
    };

} // namespace puuyapu
