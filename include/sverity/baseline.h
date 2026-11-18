// Baseline files: the identity of the findings a team accepts at adoption, so that later runs gate only on what is new
// (Report::setBaseline matches them). Written by --write-baseline, read by --baseline.
#pragma once
#include <string>
#include <vector>

#include "sverity/json.h"
#include "sverity/report.h"

namespace sverity {

/// What a baseline was written for: runs against another top or deck are not comparable, so a mismatch is an error.
struct BaselineContext {
    std::string top, deck;
    bool known = true;   // false for a report with no recorded inputs: nothing to compare
};

struct BaselineFile {
    BaselineContext context;
    std::vector<BaselineEntry> entries;
};

/// The context of a run: the top modules the design elaborated to (`elaboratedTops`, from the design itself) or, when a run did not
/// get that far or an older report records none, the `--top` names among the slang arguments (sorted, comma-joined; "(not named)"
/// when there is none), and the deck.
BaselineContext baselineContextOf(const std::vector<std::string>& slangArgs, const std::string& deck,
                                  const std::vector<std::string>& elaboratedTops = {});

/// `sverity-baseline/1`: the context and one entry per finding that is not waived or resolved, in report order.
json::Value baselineToJson(const Report& report, const BaselineContext& context);

/// Reads a `sverity-baseline/1` file, or (for a baseline made of an earlier run's report) an `sverity-report/1` JSON, from which
/// the fingerprints, rules, files, lines and first signals are taken. False with `error` set when the text is neither, or when a
/// list or fingerprint is missing: a baseline that cannot be read exactly must not read as an empty one.
bool parseBaseline(const std::string& text, BaselineFile& out, std::string& error);

/// Empty when `baseline` and `run` are comparable, else the reason they are not.
std::string baselineMismatch(const BaselineContext& baseline, const BaselineContext& run);

} // namespace sverity
