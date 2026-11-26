// Three-valued (0/1/X) evaluation of a register's next-state input at the first clock edge after its asynchronous resets release.
//
// The procedure of the register is interpreted over its statement tree: assignments, if/else, case, blocks. A value is 0, 1 or X, with a
// taint flag that says the value depends on a source the caller marked: a known result reached through a controlling constant
// (`x & 1'b0`, a condition decided by known operands) is untainted whatever its other operands are. Anything the interpreter cannot read
// (a call, a loop, an unsupported operator) gives X tainted and clears `ok`.
#pragma once
#include "domains/model.h"
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

namespace sverity::rdc {

struct Tri {
    uint8_t v = 2;       // 0, 1 or 2 (X)
    bool taint = false;  // depends on a tainted source
    bool known() const { return v < 2; }
};

class ReleaseEvaluator {
public:
    explicit ReleaseEvaluator(const model::Model& m) : m_(m) {}
    /// Value of a model node during the evaluation. Called for every register or net bit the procedure reads.
    using Lookup = std::function<Tri(model::NodeId)>;
    /// The value register `fi` takes at the first edge after release, given `env` for every other bit. `reset` signals of the register itself
    /// are the caller's to set to their released level in `env`. `ok` is false when part of the procedure could not be interpreted.
    Tri nextValue(uint32_t fi, const Lookup& env, bool& ok);

private:
    const model::Model& m_;
    std::map<std::pair<const slang::ast::InstanceSymbol*, const slang::ast::ValueSymbol*>, std::vector<model::NodeId>> index_;
    bool indexed_ = false;
    void buildIndex();
    friend struct EvalRun;
};

} // namespace sverity::rdc
