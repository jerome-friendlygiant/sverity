#include "sverity/timings.h"

#include <sys/resource.h>

#include "sverity/json.h"

namespace sverity {

void Timings::record(std::string phase, Clock::time_point start) {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    entries_.push_back({std::move(phase), std::chrono::duration<double>(Clock::now() - start).count(), static_cast<uint64_t>(usage.ru_maxrss)});
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
