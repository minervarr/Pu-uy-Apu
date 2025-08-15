/**
 * @file data_processor.cpp
 * @brief Implementation of high-performance data processing
 *
 * Optimized JSON/CSV generation with minimal memory allocation.
 */

#include "data_processor.h"
#include "time_utils.h"
#include <iomanip>
#include <sstream>
#include <cstring>

namespace puuyapu {

    std::string DataProcessor::exportToJSON(
            const std::vector<SleepDetectionResult>& sessions,
            bool include_debug) noexcept {

        std::ostringstream json;
        json << "{\n";
        json << "  \"export_timestamp\": \"" << timestampToISO(std::chrono::system_clock::now()) << "\",\n";
        json << "  \"total_sessions\": " << sessions.size() << ",\n";
        json << "  \"include_debug\": " << (include_debug ? "true" : "false") << ",\n";
        json << "  \"sleep_sessions\": [\n";

        for (size_t i = 0; i < sessions.size(); ++i) {
            const auto& session = sessions[i];

            json << "    {\n";

            if (session.bedtime.has_value()) {
                json << "      \"bedtime\": \"" << timestampToISO(session.bedtime.value()) << "\",\n";
            }

            if (session.wake_time.has_value()) {
                json << "      \"wake_time\": \"" << timestampToISO(session.wake_time.value()) << "\",\n";
            }

            json << "      \"duration_hours\": " << doubleToString(session.duration.count()) << ",\n";
            json << "      \"confidence\": \"" << session.getConfidenceString() << "\",\n";
            json << "      \"quality_score\": " << doubleToString(session.quality_score) << ",\n";
            json << "      \"manually_confirmed\": " << (session.is_manually_confirmed ? "true" : "false") << ",\n";
            json << "      \"pattern_match_score\": " << doubleToString(session.pattern_match_score) << ",\n";
            json << "      \"sleep_efficiency\": " << doubleToString(session.calculateSleepEfficiency()) << ",\n";
            json << "      \"interruptions_count\": " << session.interruptions.size();

            if (include_debug && !session.interruptions.empty()) {
                json << ",\n      \"interruptions\": [\n";
                for (size_t j = 0; j < session.interruptions.size(); ++j) {
                    const auto& interruption = session.interruptions[j];
                    json << "        {\n";
                    json << "          \"timestamp\": \"" << timestampToISO(interruption.timestamp) << "\",\n";
                    json << "          \"duration_ms\": " << interruption.duration.count() << ",\n";
                    json << "          \"is_brief_check\": " << (interruption.is_brief_check ? "true" : "false") << ",\n";
                    json << "          \"impact_score\": " << doubleToString(interruption.impact_score) << "\n";
                    json << "        }";
                    if (j < session.interruptions.size() - 1) json << ",";
                    json << "\n";
                }
                json << "      ]";
            }

            json << "\n    }";
            if (i < sessions.size() - 1) json << ",";
            json << "\n";
        }

        json << "  ]\n";
        json << "}";

        return json.str();
    }

    std::string DataProcessor::exportToCSV(
            const std::vector<SleepDetectionResult>& sessions) noexcept {

        std::ostringstream csv;

        // Header
        csv << "Date,Bedtime,WakeTime,DurationHours,Confidence,QualityScore,";
        csv << "ManuallyConfirmed,PatternMatch,SleepEfficiency,InterruptionsCount\n";

        // Data rows
        for (const auto& session : sessions) {
            if (!session.isValid()) continue;

            // Extract date from bedtime
            auto time_t = std::chrono::system_clock::to_time_t(session.bedtime.value());
            auto tm = *std::localtime(&time_t);

            csv << (tm.tm_year + 1900) << "-"
                << std::setfill('0') << std::setw(2) << (tm.tm_mon + 1) << "-"
                << std::setfill('0') << std::setw(2) << tm.tm_mday << ",";

            // Bedtime
            csv << timestampToISO(session.bedtime.value()) << ",";

            // Wake time
            if (session.wake_time.has_value()) {
                csv << timestampToISO(session.wake_time.value());
            }
            csv << ",";

            // Duration, confidence, quality
            csv << doubleToString(session.duration.count()) << ",";
            csv << session.getConfidenceString() << ",";
            csv << doubleToString(session.quality_score) << ",";
            csv << (session.is_manually_confirmed ? "true" : "false") << ",";
            csv << doubleToString(session.pattern_match_score) << ",";
            csv << doubleToString(session.calculateSleepEfficiency()) << ",";
            csv << session.interruptions.size() << "\n";
        }

        return csv.str();
    }

    std::string DataProcessor::exportPerformanceMetrics(
            const std::unordered_map<std::string, std::chrono::microseconds>& metrics) noexcept {

        std::ostringstream json;
        json << "{\n";
        json << "  \"timestamp\": \"" << timestampToISO(std::chrono::system_clock::now()) << "\",\n";
        json << "  \"metrics\": {\n";

        size_t count = 0;
        for (const auto& [operation, duration] : metrics) {
            json << "    \"" << operation << "\": " << duration.count();
            if (++count < metrics.size()) json << ",";
            json << "\n";
        }

        json << "  }\n";
        json << "}";

        return json.str();
    }

    size_t DataProcessor::serializeToBinary(
            const SleepDetectionResult& session,
            uint8_t* buffer) noexcept {

        if (!buffer || !session.isValid()) {
            return 0;
        }

        size_t offset = 0;

        // Bedtime timestamp (8 bytes)
        auto bedtime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                session.bedtime.value().time_since_epoch()).count();
        std::memcpy(buffer + offset, &bedtime_ms, sizeof(int64_t));
        offset += sizeof(int64_t);

        // Wake time timestamp (8 bytes)
        int64_t wake_ms = 0;
        if (session.wake_time.has_value()) {
            wake_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    session.wake_time.value().time_since_epoch()).count();
        }
        std::memcpy(buffer + offset, &wake_ms, sizeof(int64_t));
        offset += sizeof(int64_t);

        // Duration in seconds (4 bytes)
        float duration_seconds = static_cast<float>(session.duration.count() * 3600.0);
        std::memcpy(buffer + offset, &duration_seconds, sizeof(float));
        offset += sizeof(float);

        // Confidence (1 byte)
        uint8_t confidence = static_cast<uint8_t>(session.confidence);
        buffer[offset++] = confidence;

        // Quality score (4 bytes)
        float quality = static_cast<float>(session.quality_score);
        std::memcpy(buffer + offset, &quality, sizeof(float));
        offset += sizeof(float);

        // Flags (1 byte)
        uint8_t flags = 0;
        if (session.is_manually_confirmed) flags |= 0x01;
        buffer[offset++] = flags;

        // Pattern match score (4 bytes)
        float pattern_score = static_cast<float>(session.pattern_match_score);
        std::memcpy(buffer + offset, &pattern_score, sizeof(float));
        offset += sizeof(float);

        // Interruptions count (2 bytes)
        uint16_t interruption_count = static_cast<uint16_t>(session.interruptions.size());
        std::memcpy(buffer + offset, &interruption_count, sizeof(uint16_t));
        offset += sizeof(uint16_t);

        return offset;
    }

    SleepDetectionResult DataProcessor::deserializeFromBinary(
            const uint8_t* buffer,
            size_t size) noexcept {

        SleepDetectionResult session;

        if (!buffer || size < 32) { // Minimum required size
            return session;
        }

        size_t offset = 0;

        // Bedtime timestamp
        int64_t bedtime_ms;
        std::memcpy(&bedtime_ms, buffer + offset, sizeof(int64_t));
        session.bedtime = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(bedtime_ms));
        offset += sizeof(int64_t);

        // Wake time timestamp
        int64_t wake_ms;
        std::memcpy(&wake_ms, buffer + offset, sizeof(int64_t));
        if (wake_ms > 0) {
            session.wake_time = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(wake_ms));
        }
        offset += sizeof(int64_t);

        // Duration
        float duration_seconds;
        std::memcpy(&duration_seconds, buffer + offset, sizeof(float));
        session.duration = std::chrono::duration<double, std::ratio<3600>>(
                duration_seconds / 3600.0);
        offset += sizeof(float);

        // Confidence
        uint8_t confidence = buffer[offset++];
        session.confidence = static_cast<SleepConfidence>(confidence);

        // Quality score
        float quality;
        std::memcpy(&quality, buffer + offset, sizeof(float));
        session.quality_score = static_cast<double>(quality);
        offset += sizeof(float);

        // Flags
        uint8_t flags = buffer[offset++];
        session.is_manually_confirmed = (flags & 0x01) != 0;

        // Pattern match score
        float pattern_score;
        std::memcpy(&pattern_score, buffer + offset, sizeof(float));
        session.pattern_match_score = static_cast<double>(pattern_score);
        offset += sizeof(float);

        return session;
    }

    std::string DataProcessor::timestampToISO(
            std::chrono::system_clock::time_point time_point) noexcept {

        auto time_t = std::chrono::system_clock::to_time_t(time_point);
        auto tm = *std::gmtime(&time_t);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");

        // Add milliseconds
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                time_point.time_since_epoch()) % 1000;
        oss << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z";

        return oss.str();
    }

    std::string DataProcessor::doubleToString(double value, int precision) noexcept {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << value;
        return oss.str();
    }

} // namespace puuyapu
