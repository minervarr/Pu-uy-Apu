// performance_monitor.h - Built-in performance tracking
class PerformanceMonitor {
private:
    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> timers;
    std::unordered_map<std::string, double> averageTimes;

public:
    void startTimer(const std::string& operation) {
        timers[operation] = std::chrono::high_resolution_clock::now();
    }

    void endTimer(const std::string& operation) {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                endTime - timers[operation]
        ).count();

        // Update running average
        averageTimes[operation] = (averageTimes[operation] + duration) / 2.0;

#ifdef DEBUG
        __android_log_print(ANDROID_LOG_DEBUG, "PuuyApu",
            "Operation %s took %ld microseconds",
            operation.c_str(), duration);
#endif
    }

    double getAverageTime(const std::string& operation) const {
        auto it = averageTimes.find(operation);
        return it != averageTimes.end() ? it->second : 0.0;
    }
};