#include "synth_pragma.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "slang/syntax/SyntaxTree.h"

namespace sverity::lint {

using slang::BufferID;
using slang::SourceLocation;

namespace {

// The pragma words a comment holds, or none: the first word names the tool (`synopsys`, `synthesis`, `pragma`, `cadence`, `ambit`,
// `rtl_synthesis`); a later word is the directive.
void classify(std::string_view text, SourceLocation at, std::vector<PragmaMark>& out) {
    std::string lowered;
    for (char c : text) lowered += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : ' ';
    std::istringstream in(lowered);
    std::vector<std::string> words;
    for (std::string w; in >> w;) words.push_back(w);
    if (words.empty()) return;
    static const char* const tools[] = {"synopsys", "synthesis", "pragma", "cadence", "ambit", "rtl_synthesis"};
    if (std::find(std::begin(tools), std::end(tools), words[0]) == std::end(tools)) return;
    // The directive follows the tool word directly; `synthesis on an FPGA ...` or `synopsys DC needs no translate_off` is prose.
    // A bare `off` / `on` counts only when it ends the comment.
    for (size_t i = 1; i < words.size(); i++) {
        const std::string& w = words[i];
        if (w == "full_case") out.push_back({PragmaMark::FullCase, at, w});
        else if (w == "parallel_case") out.push_back({PragmaMark::ParallelCase, at, w});
        else if (w == "translate_off" || w == "synthesis_off") out.push_back({PragmaMark::Off, at, w});
        else if (w == "translate_on" || w == "synthesis_on") out.push_back({PragmaMark::On, at, w});
        else if ((w == "off" || w == "on") && i + 1 == words.size() &&
                 (words[0] == "rtl_synthesis" || (words[0] == "ambit" && i == 2 && words[1] == "synthesis") || (words[0] == "synthesis" && i == 1)))
            out.push_back({w == "off" ? PragmaMark::Off : PragmaMark::On, at, words[0] + " " + w});
        else if (i == 1 && words[0] == "ambit" && w == "synthesis") continue;
        else break;
    }
}

} // namespace

bool PragmaIndex::inRegion(SourceLocation at) const {
    if (!at.valid()) return false;
    for (const auto& r : regions)
        if (r.begin.buffer().getId() == at.buffer().getId() && at.offset() >= r.begin.offset() && at.offset() < r.end) return true;
    return false;
}

PragmaIndex readPragmas(const slang::SourceManager& sm, const std::vector<BufferID>& buffers) {
    PragmaIndex index;
    for (BufferID buffer : buffers) {
        const std::string_view text = sm.getSourceText(buffer);
        // Cheap test before reading the buffer: a pragma comment names its tool.
        const auto has = [&](std::string_view w) { return text.find(w) != std::string_view::npos; };
        index.caseWords = index.caseWords || has("full_case") || has("parallel_case");
        if (!has("synopsys") && !has("synthesis") && !has("pragma") && !has("cadence") && !has("ambit")) continue;
        std::vector<PragmaMark> found;
        for (size_t i = 0; i < text.size();) {
            if (text[i] == '"') {   // a string: its text is not a comment
                for (i++; i < text.size() && text[i] != '"' && text[i] != '\n'; i++)
                    if (text[i] == '\\') i++;
                i++;
            }
            else if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
                const size_t end = std::min(text.find('\n', i), text.size());
                classify(text.substr(i + 2, end - i - 2), SourceLocation(buffer, i), found);
                i = end;
            }
            else if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
                const size_t end = std::min(text.find("*/", i + 2), text.size());
                classify(text.substr(i + 2, end - i - 2), SourceLocation(buffer, i), found);
                i = std::min(end + 2, text.size());
            }
            else i++;
        }
        bool open = false;
        SourceLocation openedAt;
        for (const auto& m : found) {
            index.marks.push_back(m);
            if (m.kind == PragmaMark::Off) {
                if (open) index.unbalanced.push_back({m, "it comes while the region opened above is still open"});
                else { open = true; openedAt = m.at; }
            }
            else if (m.kind == PragmaMark::On) {
                if (!open) { index.unbalanced.push_back({m, "there is no translate_off before it"}); continue; }
                index.regions.push_back({openedAt, m.at.offset(), true});
                open = false;
            }
        }
        if (open) {
            index.regions.push_back({openedAt, text.size(), false});
            index.unbalanced.push_back({{PragmaMark::Off, openedAt, "translate_off"}, "the file ends before a translate_on"});
        }
    }
    return index;
}

PragmaIndex readUserPragmas(const slang::ast::Compilation& compilation) {
    std::vector<BufferID> buffers;
    auto add = [&](BufferID id) {
        if (std::none_of(buffers.begin(), buffers.end(), [&](BufferID b) { return b.getId() == id.getId(); })) buffers.push_back(id);
    };
    for (const auto& t : compilation.getSyntaxTrees()) {
        if (!t || t->isLibraryUnit) continue;
        add(t->root().sourceRange().start().buffer());
        for (auto id : t->getSourceBufferIds()) add(id);
    }
    return readPragmas(*compilation.getSourceManager(), buffers);
}

} // namespace sverity::lint
