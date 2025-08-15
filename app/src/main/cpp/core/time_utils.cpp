/**
 * @file time_utils.cpp
 * @brief Implementation of time utility functions
 *
 * Fast time calculations optimized for sleep tracking operations.
 */

#include "time_utils.h"
#include <ctime>
#include <chrono>

namespace puuyapu {

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

    double calculateDurationHours(
            const std::chrono::system_clock::time_point& start,
            const std::chrono::system_clock::time_point& end) noexcept {

        auto duration = end - start;
        return std::chrono::duration<double, std::ratio<3600>>(duration).count();
    }

} // namespace puuyapu