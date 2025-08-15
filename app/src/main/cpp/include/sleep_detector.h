// sleep_detector.h - Core detection engine
class SleepDetector {
private:
    std::vector<InteractionEvent> interactions;
    UserPreferences preferences;
    std::chrono::system_clock::time_point lastMeaningfulInteraction;

public:
    // High-performance sleep detection
    SleepDetectionResult detectSleepPeriod(
            const std::vector<InteractionEvent>& events,
            const std::chrono::system_clock::time_point& currentTime
    );

    // Real-time confidence scoring
    double calculateConfidenceScore(const SleepSession& session);

    // Pattern matching algorithms
    bool matchesHistoricalPattern(const SleepSession& session);

    // Fast interruption analysis
    std::vector<SleepInterruption> analyzeInterruptions(
            const std::vector<InteractionEvent>& events
    );
};

// interaction_analyzer.h - Real-time interaction processing
class InteractionAnalyzer {
public:
    // Classify interaction types in microseconds
    InteractionType classifyInteraction(
            const InteractionEvent& event,
            const std::vector<InteractionEvent>& recentHistory
    );

    // Fast gap detection
    std::vector<TimeGap> detectInteractionGaps(
            const std::vector<InteractionEvent>& events,
            std::chrono::seconds minimumGap
    );

    // Real-time pattern recognition
    bool isTimeCheck(const InteractionEvent& event);
    bool isMeaningfulUsage(const InteractionEvent& event);
};