/**
 * @file interaction_analyzer.cpp
 * @brief Implementation of real-time interaction analysis
 *
 * High-performance implementation optimized for Android ARM processors.
 * All algorithms designed for sub-millisecond execution times.
 */

#include "interaction_analyzer.h"
#include <algorithm>
#include <cmath>

namespace puuyapu {

    InteractionType InteractionAnalyzer::classifyInteraction(
            const InteractionEvent& event,
            const InteractionEventList& context) noexcept {

        // If already classified, return existing classification
        if (event.type != InteractionType::UNKNOWN) {
            return event.type;
        }

        // Fast duration-based classification
        const auto duration_ms = event.duration.count();

        // Very short interactions are time checks
        if (duration_ms < 10000) { // < 10 seconds
            return InteractionType::TIME_CHECK;
        }

        // Short interactions need context analysis
        if (duration_ms < 30000) { // < 30 seconds
            // Check if this follows a recent meaningful interaction
            if (!context.empty()) {
                const auto& last_event = context.back();
                auto time_gap = event.timestamp - last_event.timestamp;

                // If close in time to meaningful use, classify as continuation
                if (time_gap < std::chrono::minutes(2) &&
                    last_event.type == InteractionType::MEANINGFUL_USE) {
                    return InteractionType::MEANINGFUL_USE;
                }
            }

            // Check app category for context
            if (event.category == AppCategory::CLOCK_ALARM) {
                return InteractionType::TIME_CHECK;
            }

            return InteractionType::TIME_CHECK;
        }

        // Medium duration interactions
        if (duration_ms < 300000) { // < 5 minutes
            return InteractionType::MEANINGFUL_USE;
        }

        // Long interactions
        return InteractionType::EXTENDED_USE;
    }

    TimeGapList InteractionAnalyzer::detectInteractionGaps(
            const InteractionEventList& events,
            std::chrono::milliseconds min_gap) noexcept {

        TimeGapList gaps;
        gaps.reserve(events.size() / 10); // Pre-allocate for performance

        if (events.size() < 2) {
            return gaps;
        }

        // Process consecutive meaningful interactions only
        auto prev_meaningful = events.begin();

        for (auto it = events.begin() + 1; it != events.end(); ++it) {
            if (isMeaningfulUsage(*it)) {
                auto gap_duration = it->timestamp - prev_meaningful->timestamp;

                if (gap_duration >= min_gap) {
                    TimeGap gap(prev_meaningful->timestamp, it->timestamp);

                    // Count brief interactions within the gap
                    int brief_count = 0;
                    for (auto gap_it = prev_meaningful + 1; gap_it != it; ++gap_it) {
                        if (isTimeCheck(*gap_it)) {
                            brief_count++;
                        }
                    }

                    gap.brief_interaction_count = brief_count;
                    gap.contains_brief_interactions = (brief_count > 0);

                    gaps.push_back(gap);
                }

                prev_meaningful = it;
            }
        }

        return gaps;
    }

} // namespace puuyapu