// sleep_detector.cpp - Optimized for speed
#include "sleep_detector.h"
#include <algorithm>
#include <chrono>
#include <vector>
#include <unordered_map>

class SleepDetector {
private:
    // Pre-allocated vectors for performance
    std::vector<InteractionEvent> eventBuffer;
    std::vector<double> confidenceScores;
    std::unordered_map<std::string, double> patternWeights;

    // Cache for frequently accessed data
    mutable std::chrono::system_clock::time_point lastCalculationTime;
    mutable SleepDetectionResult cachedResult;

public:
    SleepDetectionResult detectSleepPeriod(
            const std::vector<InteractionEvent>& events,
            const std::chrono::system_clock::time_point& currentTime
    ) {
        // Fast path: check if we can use cached result
        if (canUseCachedResult(currentTime)) {
            return cachedResult;
        }

        // High-performance detection algorithm
        auto sleepStart = findSleepStartTime(events);
        auto sleepEnd = findSleepEndTime(events, currentTime);

        if (sleepStart && sleepEnd) {
            SleepDetectionResult result;
            result.bedtime = *sleepStart;
            result.wakeTime = *sleepEnd;
            result.duration = calculateDuration(*sleepStart, *sleepEnd);
            result.confidence = calculateConfidenceScore(events, *sleepStart, *sleepEnd);
            result.interruptions = analyzeInterruptions(events, *sleepStart, *sleepEnd);

            // Cache result for performance
            cachedResult = result;
            lastCalculationTime = currentTime;

            return result;
        }

        return SleepDetectionResult{}; // No sleep detected
    }

private:
    // Optimized algorithms using C++ STL
    std::optional<std::chrono::system_clock::time_point> findSleepStartTime(
            const std::vector<InteractionEvent>& events
    ) {
        // Fast reverse iteration to find last meaningful interaction
        auto it = std::find_if(events.rbegin(), events.rend(),
                               [](const InteractionEvent& e) {
                                   return e.type == InteractionType::MEANINGFUL_USE;
                               });

        if (it != events.rend()) {
            return it->timestamp;
        }
        return std::nullopt;
    }

    double calculateConfidenceScore(
            const std::vector<InteractionEvent>& events,
            const std::chrono::system_clock::time_point& sleepStart,
            const std::chrono::system_clock::time_point& sleepEnd
    ) {
        double score = 0.0;

        // Fast confidence calculation using pre-computed weights
        score += patternWeights["duration_match"] *
                 evaluateDurationMatch(sleepStart, sleepEnd);
        score += patternWeights["interruption_penalty"] *
                 evaluateInterruptions(events, sleepStart, sleepEnd);
        score += patternWeights["pattern_consistency"] *
                 evaluatePatternConsistency(sleepStart);

        return std::clamp(score, 0.0, 1.0);
    }
};