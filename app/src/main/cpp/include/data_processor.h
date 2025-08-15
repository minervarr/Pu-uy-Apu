/**
 * @file data_processor.h
 * @brief High-performance data processing and export functionality
 *
 * Handles conversion between C++ native types and Java-compatible formats.
 * Optimized for minimal copying and maximum throughput.
 *
 * @performance Target: < 1ms for data export operations
 */

#pragma once

#include "puuyapu_types.h"
#include <string>
#include <sstream>

namespace puuyapu {

    /**
     * @brief High-performance data processor for export and serialization
     *
     * Converts native C++ sleep data to JSON/CSV formats optimized for
     * minimal memory allocation and maximum processing speed.
     */
    class DataProcessor {
    public:
        /**
         * @brief Export sleep sessions to JSON format
         * @param sessions Vector of sleep detection results
         * @param include_debug Include debug information in export
         * @return JSON string with all sleep data
         * @performance < 1ms per 100 sessions
         */
        static std::string exportToJSON(
                const std::vector<SleepDetectionResult>& sessions,
                bool include_debug = false) noexcept;

        /**
         * @brief Export sleep sessions to CSV format
         * @param sessions Vector of sleep detection results
         * @return CSV string with sleep data
         * @performance < 500μs per 100 sessions
         */
        static std::string exportToCSV(
                const std::vector<SleepDetectionResult>& sessions) noexcept;

        /**
         * @brief Export performance metrics to JSON
         * @param metrics Performance timing data
         * @return JSON string with performance data
         * @performance < 100μs
         */
        static std::string exportPerformanceMetrics(
                const std::unordered_map<std::string, std::chrono::microseconds>& metrics) noexcept;

        /**
         * @brief Convert sleep session to compact binary format
         * @param session Sleep session to serialize
         * @param buffer Output buffer (must be at least 128 bytes)
         * @return Number of bytes written
         * @performance < 50μs
         */
        static size_t serializeToBinary(
                const SleepDetectionResult& session,
                uint8_t* buffer) noexcept;

        /**
         * @brief Deserialize sleep session from binary format
         * @param buffer Input buffer containing serialized data
         * @param size Number of bytes in buffer
         * @return Deserialized sleep session
         * @performance < 50μs
         */
        static SleepDetectionResult deserializeFromBinary(
                const uint8_t* buffer,
                size_t size) noexcept;

    private:
        /**
         * @brief Fast timestamp to ISO string conversion
         * @param time_point Timestamp to convert
         * @return ISO 8601 formatted timestamp string
         * @performance < 20μs
         */
        static std::string timestampToISO(
                std::chrono::system_clock::time_point time_point) noexcept;

        /**
         * @brief Optimized double to string conversion
         * @param value Double value to convert
         * @param precision Number of decimal places
         * @return String representation
         * @performance < 10μs
         */
        static std::string doubleToString(double value, int precision = 2) noexcept;
    };

} // namespace puuyapu