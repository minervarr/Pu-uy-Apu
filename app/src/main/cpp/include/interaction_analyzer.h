/**
 * @file interaction_analyzer.h
 * @brief Real-time interaction classification and analysis engine
 *
 * Optimized for microsecond-level performance on Android devices.
 * Classifies phone interactions to distinguish sleep patterns.
 *
 * @performance Target: < 50μs per interaction classification
 */

#pragma once

#include "puuyapu_types.h"
#include <vector>
#include <chrono>

namespace puuyapu {

    /**
     * @brief High-speed interaction analyzer for real-time processing
     *
     * Stateless analyzer optimized for concurrent usage from multiple threads.
     * All methods are thread-safe and lock-free for maximum performance.
     */
    class InteractionAnalyzer {
    public:
        /**
         * @brief Classify interaction type in real-time
         * @param event Interaction event to classify
         * @param context Recent events for contextual analysis
         * @return Classified interaction type
         * @performance < 50 microseconds
         */
        static InteractionType classifyInteraction(
                const InteractionEvent& event,
                const InteractionEventList& context) noexcept;

        /**
         * @brief Detect significant gaps in interaction patterns
         * @param events Chronologically sorted interaction events
         * @param min_gap Minimum gap duration to consider significant
         * @return List of detected time gaps
         * @performance < 500 microseconds for 1000 events
         */
        static TimeGapList detectInteractionGaps(
                const InteractionEventList& events,
                std::chrono::milliseconds min_gap) noexcept;

        /**
         * @brief Fast time check detection
         * @param event Event to analyze
         * @return true if event represents brief time check
         * @performance < 10 microseconds
         */
        static inline bool isTimeCheck(const InteractionEvent& event) noexcept {
            return event.duration.count() < 15000 || // < 15 seconds
                   event.type == InteractionType::TIME_CHECK ||
                   (event.category == AppCategory::CLOCK_ALARM &&
                    event.duration.count() < 30000);
        }

        /**
         * @brief Fast meaningful usage detection
         * @param event Event to analyze
         * @return true if event represents active phone usage
         * @performance < 10 microseconds
         */
        static inline bool isMeaningfulUsage(const InteractionEvent& event) noexcept {
            return event.duration.count() >= 30000 || // >= 30 seconds
                   event.type == InteractionType::MEANINGFUL_USE ||
                   event.type == InteractionType::EXTENDED_USE ||
                   event.type == InteractionType::NOTIFICATION_RESPONSE;
        }
    };

} // namespace puuyapu
