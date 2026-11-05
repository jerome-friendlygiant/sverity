// Optional resource measurements of one run. They vary between runs, so they never enter the deterministic report.
#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace sverity {

struct TimingEntry {
    std::string phase;
    double seconds = 0;
    uint64_t peakRssKb = 0;   // process peak so far, not this phase's own use
};

class Timings {
public:
    using Clock = std::chrono::steady_clock;
    void record(std::string phase, Clock::time_point start);
    void count(std::string name, uint64_t value) { counts_.emplace_back(std::move(name), value); }
    std::string toJson() const;

private:
    std::vector<TimingEntry> entries_;
    std::vector<std::pair<std::string, uint64_t>> counts_;
};

} // namespace sverity
