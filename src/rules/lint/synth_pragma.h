// Synthesis pragmas written as comments (`// synopsys translate_off`, `// synopsys full_case`): slang reads them as comments, a synthesis tool does not.
#pragma once
#include <string>
#include <vector>

#include "slang/ast/Compilation.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

namespace sverity::lint {

struct PragmaMark {
    enum Kind { FullCase, ParallelCase, Off, On } kind;
    slang::SourceLocation at;   // the comment
    std::string spelling;       // the word written, as for a message
};

/// A `translate_off` ... `translate_on` span of one source buffer. `closed` is false when the file ends first (the region then
/// runs to its end).
struct PragmaRegion {
    slang::SourceLocation begin;
    size_t end = 0;   // offset of the closing comment, or of the end of the buffer
    bool closed = false;
};

struct PragmaIndex {
    std::vector<PragmaMark> marks;       // every pragma comment, by buffer then offset
    std::vector<PragmaRegion> regions;
    bool caseWords = false;   // some buffer names full_case or parallel_case (a comment or an attribute may carry the pragma)
    struct Unbalanced { PragmaMark mark; std::string why; };
    std::vector<Unbalanced> unbalanced;   // an `on` with no `off` before it, an `off` inside an `off`, an `off` the file never closes
    /// True when `at` (a use-site location) lies inside a region.
    bool inRegion(slang::SourceLocation at) const;
};

/// Reads the pragma comments of the given source buffers. Comment text excluded by a preprocessor condition is still read.
PragmaIndex readPragmas(const slang::SourceManager& sm, const std::vector<slang::BufferID>& buffers);

/// The pragmas of every user source file of the design (a library unit is not read).
PragmaIndex readUserPragmas(const slang::ast::Compilation& compilation);

} // namespace sverity::lint
