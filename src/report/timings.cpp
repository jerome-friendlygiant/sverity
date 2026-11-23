#include "sverity/timings.h"

#include <algorithm>
#include <sys/resource.h>

#include <fstream>

#include "sverity/json.h"

namespace sverity {

void Timings::record(std::string phase, Clock::time_point start) {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    entries_.push_back({std::move(phase), std::chrono::duration<double>(Clock::now() - start).count(), static_cast<uint64_t>(usage.ru_maxrss)});
    flush();
}

void Timings::count(std::string name, uint64_t value) {
    counts_.emplace_back(std::move(name), value);
    flush();
}

void Timings::checkpoint(const std::vector<std::pair<std::string, uint64_t>>& values) {
    const auto update = [&](const std::string& name, uint64_t value) {
        auto found = std::find_if(counts_.begin(), counts_.end(), [&](const auto& item) { return item.first == name; });
        if (found == counts_.end()) counts_.emplace_back(name, value);
        else found->second = value;
    };
    for (const auto& [name, value] : values) update(name, value);
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    update("rss_kb_checkpoint", static_cast<uint64_t>(usage.ru_maxrss));
    flush();
}

void Timings::flush() {
    if (file_.empty()) return;
    std::ofstream out(file_, std::ios::trunc);
    out << toJson() << '\n';
    writeFailed_ = !out;
}

std::string Timings::toJson() const {
    json::Value root = json::Value::object();
    root.set("schema", "sverity-timings/1");
    json::Value phases = json::Value::array();
    for (const auto& entry : entries_) {
        json::Value phase = json::Value::object();
        phase.set("phase", entry.phase).set("seconds", entry.seconds).set("peak_rss_kb", static_cast<int64_t>(entry.peakRssKb));
        phases.push(phase);
    }
    root.set("phases", phases);
    json::Value counts = json::Value::object();
    for (const auto& [name, value] : counts_) counts.set(name, static_cast<int64_t>(value));
    root.set("counts", counts);
    return root.dump(2);
}

} // namespace sverity
