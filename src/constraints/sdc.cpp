// SDC (Tcl subset) parser.
//
// Scanner preserves word quoting; the bounded evaluator substitutes before dispatch.
// Each command is transactional: diagnostics survive, constraint mutations do not.
#include "sverity/sdc.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

namespace sverity::sdc {

void Constraints::merge(Constraints&& o) {
    auto mv = [](auto& dst, auto& src) { for (auto& x : src) dst.push_back(std::move(x)); };
    mv(sourcedFiles, o.sourcedFiles);
    mv(clocks, o.clocks); mv(clockGroups, o.clockGroups); mv(exceptions, o.exceptions); mv(inputDelays, o.inputDelays);
    mv(outputDelays, o.outputDelays); mv(caseAnalyses, o.caseAnalyses);
    mv(unsupported, o.unsupported); mv(problems, o.problems);
}

namespace {

// Nesting limit for [..] substitution and for re-reading object lists. Deeper input is reported and left
// unparsed, which keeps recursion bounded on hostile input.
constexpr int kMaxDepth = 64;

enum class ArgKind { Bare, Brace, Quoted, Bracket };

struct Arg {
    ArgKind kind = ArgKind::Bare;
    std::string value;     // Bare/Quoted/Brace: the word text. Bracket: raw text between the brackets.
    size_t content = 0;
    std::string script;
    std::vector<Arg> sub;  // Bracket only: the words of the command inside the brackets
};

struct Cmd {
    std::vector<Arg> words;
    size_t begin = 0, end = 0;  // extent in the top-level text; used to rebuild `unsupported` text
    SourceLocation loc;
    bool valid = true;
};

// State shared by one parse() call.
struct Ctx {
    Constraints& out;
    std::string_view src;  // whole input
    std::string file;
    SourceLocation cur;    // command being tokenised or interpreted; problems are reported here
    std::unordered_map<std::string, Arg> variables;
    SourceLocation base;
    size_t iterations = 0;
    std::vector<std::filesystem::path> sourceStack;
};

void addProblem(Ctx& ctx, std::string msg) { ctx.out.problems.push_back({std::move(msg), ctx.cur}); }

// Keeps messages readable when they quote user text that may be very long.
std::string shortText(std::string_view s) {
    constexpr size_t kMax = 48;
    if (s.size() <= kMax) return std::string(s);
    return std::string(s.substr(0, kMax)) + "...";
}

bool isHSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }

// Length of a backslash-newline continuation at `p` (0 if there is none).
size_t contLen(std::string_view s, size_t p) {
    if (p + 1 >= s.size() || s[p] != '\\') return 0;
    if (s[p + 1] == '\n') return 2;
    if (s[p + 1] == '\r' && p + 2 < s.size() && s[p + 2] == '\n') return 3;
    return 0;
}

struct Span { size_t end; bool closed; };
enum class Stop { EndOfInput, Newline, Semicolon, CloseBracket };
struct WordsEnd { Stop stop; size_t end; };  // `end`: offset where the command text stops, before its terminator

class Scanner {
public:
    // `lineStarts` is non-null only for the top-level text; it is used to compute command locations.
    Scanner(std::string_view src, Ctx& ctx, const std::vector<size_t>* lineStarts, int depth)
        : src_(src), ctx_(ctx), lineStarts_(lineStarts), depth_(depth) {}

    // Parses commands until end of input or, when inBracket, until the ']' that closes the substitution.
    std::vector<Cmd> parseScript(bool inBracket);

    // Reads words until a command terminator (command mode) or end of input. In list mode newlines and
    // blanks both separate words, and ';' is an ordinary character.
    WordsEnd scanWords(std::vector<Arg>& words, bool listMode, bool inBracket);

private:
    void skipBlanks();
    void skipComment();
    Arg parseWord(bool listMode, bool inBracket);
    bool atWordEnd(bool listMode, bool inBracket) const;
    Arg parseBraced();
    Arg parseQuoted(bool listMode);
    Arg parseBracketWord();
    Arg parseBare(bool listMode, bool inBracket);
    Span skipBrackets(size_t p) const;
    SourceLocation locationAt(size_t pos) const;

    std::string_view src_;
    Ctx& ctx_;
    const std::vector<size_t>* lineStarts_;
    int depth_;
    size_t pos_ = 0;
    bool closed_ = false;  // set by parseScript(true): the bracket was closed by ']'
};

void Scanner::skipBlanks() {
    for (;;) {
        if (pos_ < src_.size() && isHSpace(src_[pos_])) {
            ++pos_;
        } else if (size_t len = contLen(src_, pos_)) {
            pos_ += len;  // backslash-newline acts as blank space
        } else {
            return;
        }
    }
}

void Scanner::skipComment() {
    while (pos_ < src_.size() && src_[pos_] != '\n') {
        if (size_t len = contLen(src_, pos_)) {
            pos_ += len;  // a backslash-newline continues the comment onto the next line
        } else if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
            pos_ += 2;    // escaped character, e.g. "\\" does not start a continuation
        } else {
            ++pos_;
        }
    }
}

std::vector<Cmd> Scanner::parseScript(bool inBracket) {
    std::vector<Cmd> cmds;
    const size_t n = src_.size();
    closed_ = false;
    for (;;) {
        // Before a command: blanks, newlines, ';' and comments. '#' starts a comment only here.
        for (;;) {
            skipBlanks();
            if (pos_ < n && (src_[pos_] == '\n' || src_[pos_] == ';')) {
                ++pos_;
            } else if (pos_ < n && src_[pos_] == '#') {
                skipComment();
            } else {
                break;
            }
        }
        if (pos_ >= n) break;
        if (inBracket && src_[pos_] == ']') {
            ++pos_;
            closed_ = true;
            break;
        }

        if (lineStarts_) ctx_.cur = locationAt(pos_);
        Cmd cmd;
        cmd.loc = ctx_.cur;
        cmd.begin = pos_;
        const size_t problems = ctx_.out.problems.size();
        WordsEnd we = scanWords(cmd.words, false, inBracket);
        cmd.valid = ctx_.out.problems.size() == problems;
        cmd.end = we.end;
        if (!cmd.words.empty()) cmds.push_back(std::move(cmd));
        if (we.stop == Stop::CloseBracket) {
            closed_ = true;
            break;
        }
        if (we.stop == Stop::EndOfInput) break;
    }
    return cmds;
}

WordsEnd Scanner::scanWords(std::vector<Arg>& words, bool listMode, bool inBracket) {
    const size_t n = src_.size();
    for (;;) {
        skipBlanks();
        if (pos_ >= n) return {Stop::EndOfInput, pos_};
        const char c = src_[pos_];
        if (c == '\n') {
            if (listMode) {
                ++pos_;
                continue;
            }
            size_t e = pos_++;
            return {Stop::Newline, e};
        }
        if (c == ';' && !listMode) {
            size_t e = pos_++;
            return {Stop::Semicolon, e};
        }
        if (c == ']' && inBracket) {
            size_t e = pos_++;
            return {Stop::CloseBracket, e};
        }
        words.push_back(parseWord(listMode, inBracket));
    }
}

Arg Scanner::parseWord(bool listMode, bool inBracket) {
    const size_t content = pos_ + 1;
    Arg a;
    switch (src_[pos_]) {
    case '{': a = parseBraced(); break;
    case '"': a = parseQuoted(listMode); break;
    case '[': if (listMode) return parseBare(listMode, inBracket);
              a = parseBracketWord(); break;
    default: return parseBare(listMode, inBracket);
    }
    a.content = content;
    // Concatenation after a closing delimiter is outside this subset; disclose and reject it.
    if (pos_ < src_.size() && !atWordEnd(listMode, inBracket)) {
        addProblem(ctx_, "extra characters after close-brace, close-quote or close-bracket were ignored");
        while (pos_ < src_.size() && !atWordEnd(listMode, inBracket)) ++pos_;
    }
    return a;
}

bool Scanner::atWordEnd(bool listMode, bool inBracket) const {
    const char c = src_[pos_];
    return isHSpace(c) || c == '\n' || (!listMode && c == ';') || (inBracket && c == ']') ||
           contLen(src_, pos_) > 0;
}

// {...}: no substitution, so the content is kept as written. A backslash escapes the next character,
// which means an escaped brace does not count towards nesting.
Arg Scanner::parseBraced() {
    const size_t n = src_.size();
    const size_t start = pos_ + 1;
    size_t p = start;
    int level = 1;
    while (p < n) {
        const char c = src_[p];
        if (c == '\\' && p + 1 < n) {
            p += 2;
            continue;
        }
        if (c == '{') {
            ++level;
        } else if (c == '}' && --level == 0) {
            break;
        }
        ++p;
    }
    Arg a;
    a.kind = ArgKind::Brace;
    if (p >= n) {
        addProblem(ctx_, "unbalanced '{': missing closing '}'");
        a.value = std::string(src_.substr(start));
        pos_ = n;
    } else {
        a.value = std::string(src_.substr(start, p - start));
        pos_ = p + 1;
    }
    a.script = a.value;
    // Backslash-newline is the sole substitution performed inside braces.
    std::string normalized;
    for (size_t i = 0; i < a.value.size();) {
        if (size_t len = contLen(a.value, i)) {
            normalized += ' '; i += len;
            while (i < a.value.size() && isHSpace(a.value[i])) ++i;
        } else if (a.value[i] == '\\' && i + 1 < a.value.size()) {
            normalized += a.value[i++]; normalized += a.value[i++];
        } else normalized += a.value[i++];
    }
    a.value = std::move(normalized);
    return a;
}

// "...": the raw text between the quotes. Bracketed substitutions inside are skipped as a unit so that a
// quote inside them does not end the word.
Arg Scanner::parseQuoted(bool listMode) {
    const size_t n = src_.size();
    size_t p = pos_ + 1;
    Arg a;
    a.kind = ArgKind::Quoted;
    bool closed = false;
    while (p < n) {
        const char c = src_[p];
        // Braced variable names may contain word/script delimiters literally.
        if (!listMode && c == '$' && p + 1 < n && src_[p + 1] == '{') {
            const size_t close = src_.find('}', p + 2);
            if (close == std::string_view::npos) {
                addProblem(ctx_, "unbalanced variable name");
                a.value.append(src_.substr(p)); p = n;
            } else {
                a.value.append(src_.substr(p, close - p + 1)); p = close + 1;
            }
            continue;
        }
        if (c == '\\' && p + 1 < n) {
            a.value.append(src_.substr(p, 2));
            p += 2;
        } else if (c == '"') {
            closed = true;
            break;
        } else if (c == '[' && !listMode) {
            Span s = skipBrackets(p);
            a.value.append(src_.substr(p, s.end - p));
            p = s.end;
        } else {
            a.value += c;
            ++p;
        }
    }
    if (closed) {
        pos_ = p + 1;
    } else {
        addProblem(ctx_, "unterminated '\"': missing closing quote");
        pos_ = n;
    }
    return a;
}

// [...]: the words of the (last) command inside are kept in `sub`; `value` holds the raw text.
Arg Scanner::parseBracketWord() {
    const size_t n = src_.size();
    const size_t start = pos_ + 1;
    Arg a;
    a.kind = ArgKind::Bracket;
    if (depth_ >= kMaxDepth) {
        addProblem(ctx_, "command substitution nested too deeply; not interpreted");
        Span s = skipBrackets(pos_);
        const size_t close = s.closed ? s.end - 1 : n;
        a.value = std::string(src_.substr(start, close - start));
        pos_ = s.end;
        return a;
    }
    Scanner inner(src_, ctx_, nullptr, depth_ + 1);
    inner.pos_ = start;
    std::vector<Cmd> cmds = inner.parseScript(true);
    const size_t close = inner.closed_ ? inner.pos_ - 1 : n;
    if (!inner.closed_) addProblem(ctx_, "unbalanced '[': missing closing ']'");
    a.value = std::string(src_.substr(start, close - start));
    if (cmds.size() != 1) addProblem(ctx_, "command substitution requires exactly one command");
    if (!cmds.empty()) a.sub = std::move(cmds.back().words);
    pos_ = inner.pos_;
    return a;
}

// A bare word runs to the next blank or terminator. Inside it, backslash pairs and [..] substitutions are
// kept as written, so "u_x/q_reg*/Q" and "a[0]" come out unchanged.
Arg Scanner::parseBare(bool listMode, bool inBracket) {
    const size_t n = src_.size();
    Arg a;
    a.kind = ArgKind::Bare;
    size_t p = pos_;
    while (p < n) {
        const char c = src_[p];
        // Braced variable names may contain word/script delimiters literally.
        if (!listMode && c == '$' && p + 1 < n && src_[p + 1] == '{') {
            const size_t close = src_.find('}', p + 2);
            if (close == std::string_view::npos) {
                addProblem(ctx_, "unbalanced variable name");
                a.value.append(src_.substr(p)); p = n;
            } else {
                a.value.append(src_.substr(p, close - p + 1)); p = close + 1;
            }
            continue;
        }
        if (isHSpace(c) || c == '\n') break;
        if (c == ';' && !listMode) break;
        if (c == ']' && inBracket) break;
        if (contLen(src_, p)) break;
        if (c == '\\') {
            const size_t len = (p + 1 < n) ? 2 : 1;
            a.value.append(src_.substr(p, len));
            p += len;
        } else if (c == '[' && !listMode) {
            Span s = skipBrackets(p);
            if (!s.closed) addProblem(ctx_, "unbalanced '[': missing closing ']'");
            a.value.append(src_.substr(p, s.end - p));
            p = s.end;
        } else {
            a.value += c;
            ++p;
        }
    }
    // Unreachable in practice: callers consume separators first. Guarantees progress regardless.
    if (p == pos_) ++p;
    pos_ = p;
    return a;
}

// From a '[' at `p`, returns the offset just past its matching ']' (or end of input if unbalanced).
Span Scanner::skipBrackets(size_t p) const {
    const size_t n = src_.size();
    int level = 0;
    while (p < n) {
        const char c = src_[p];
        if (c == '\\' && p + 1 < n) {
            p += 2;
            continue;
        }
        if (c == '[') {
            ++level;
        } else if (c == ']' && --level == 0) {
            return {p + 1, true};
        }
        ++p;
    }
    return {n, false};
}

SourceLocation Scanner::locationAt(size_t pos) const {
    const auto& starts = *lineStarts_;
    const auto it = std::upper_bound(starts.begin(), starts.end(), pos);
    const size_t line = static_cast<size_t>(it - starts.begin());  // 1-based
    const size_t lineStart = starts[line - 1];
    return SourceLocation{ctx_.file, static_cast<uint32_t>(line + ctx_.base.line),
                          static_cast<uint32_t>(pos - lineStart + 1 + (line == 1 ? ctx_.base.column : 0))};
}

// Parses `text` as a Tcl list (used for the words of a brace group or a quoted word).
std::vector<Arg> listElements(Ctx& ctx, std::string_view text, int depth) {
    std::vector<Arg> words;
    Scanner s(text, ctx, nullptr, depth);
    s.scanWords(words, /*listMode=*/true, /*inBracket=*/false);
    for (const auto& word : words) {
        if (word.kind != ArgKind::Brace && word.value.find('\\') != std::string::npos)
            addProblem(ctx, "backslash escapes in list elements are not interpreted");
    }
    return words;
}

// Source text of a command with each backslash-newline (and the blanks that follow it) replaced by one
// space, as Tcl itself reads it.
std::string joinedText(std::string_view src, size_t begin, size_t end) {
    std::string out;
    size_t i = begin;
    while (i < end) {
        if (size_t len = contLen(src, i)) {
            while (!out.empty() && isHSpace(out.back())) out.pop_back();
            out += ' ';
            i += len;
            while (i < end && isHSpace(src[i])) ++i;
            continue;
        }
        out += src[i++];
    }
    while (!out.empty() && isHSpace(out.back())) out.pop_back();
    return out;
}

// ---------------------------------------------------------------------------------------------------------
// Object lists: names, and [get_*] commands (kind = the command name, pattern = its argument).

bool isObjectCommand(const std::string& name) {
    return name == "get_ports" || name == "get_pins" || name == "get_nets" || name == "get_clocks" ||
           name == "get_cells";
}

// Option words look like "-name". "-5" is a number and stays a positional value.
bool isOptionWord(const Arg& a) {
    return a.kind != ArgKind::Bracket && a.value.size() >= 2 && a.value[0] == '-' &&
           (std::isalpha(static_cast<unsigned char>(a.value[1])) || a.value[1] == '_');
}

void expandObjects(Ctx& ctx, const Arg& a, std::vector<ObjectRef>& out, int depth);

// [get_ports a b], [get_clocks -of_objects x], [all_clocks]: one ObjectRef per pattern. Non-option
// arguments are read as a list, so "{a b}" gives two refs.
void expandGetCommand(Ctx& ctx, const Arg& bracket, std::vector<ObjectRef>& out, int depth) {
    if (bracket.sub.empty()) {
        addProblem(ctx, "empty [] where an object was expected");
        return;
    }
    const std::string& cmdName = bracket.sub[0].value;
    if (cmdName == "all_inputs") {
        if (bracket.sub.size() != 1) addProblem(ctx, "all_inputs: options and arguments are unsupported");
        else out.push_back(ObjectRef{cmdName, ""});
        return;
    }
    if (cmdName == "all_clocks") {
        out.push_back(ObjectRef{cmdName, ""});
        return;
    }
    if (!isObjectCommand(cmdName)) {
        addProblem(ctx, "unsupported object command '" + shortText(cmdName) +
                            "' (expected get_ports, get_pins, get_nets, get_clocks, get_cells or all_clocks)");
        return;
    }
    const size_t before = out.size();
    for (size_t i = 1; i < bracket.sub.size(); ++i) {
        if (isOptionWord(bracket.sub[i])) continue;  // e.g. -hierarchical; its value is then a pattern
        std::vector<ObjectRef> names;
        expandObjects(ctx, bracket.sub[i], names, depth + 1);
        for (const ObjectRef& r : names) {
            if (r.kind == "all_inputs") addProblem(ctx, "all_inputs: nested object collections are unsupported");
            else out.push_back(ObjectRef{cmdName, r.pattern});
        }
    }
    if (out.size() == before) addProblem(ctx, "'" + cmdName + "' needs an object name");
}

// A bare name becomes ObjectRef{"", name}; a [get_*] element becomes its refs.
void expandObjects(Ctx& ctx, const Arg& a, std::vector<ObjectRef>& out, int depth) {
    if (depth > kMaxDepth) {
        addProblem(ctx, "object list nested too deeply; not interpreted");
        return;
    }
    if (a.kind == ArgKind::Bracket) {
        expandGetCommand(ctx, a, out, depth);
        return;
    }
    for (const Arg& e : listElements(ctx, a.value, depth)) {
        if (e.kind == ArgKind::Bracket) {
            expandGetCommand(ctx, e, out, depth);
        } else {
            out.push_back(ObjectRef{"", e.value});
        }
    }
}

// ---------------------------------------------------------------------------------------------------------
// Options.

struct OptSpec {
    const char* name;
    bool takesValue;
};

// Options of one command, split into value options, boolean flags and positional words.
struct Parsed {
    std::vector<std::pair<std::string, const Arg*>> valued;  // in command order (repeats allowed)
    std::vector<std::string> flags;
    std::vector<const Arg*> positional;

    std::vector<const Arg*> values(std::string_view name) const {
        std::vector<const Arg*> r;
        for (const auto& [k, v] : valued) {
            if (std::string_view(k) == name) r.push_back(v);
        }
        return r;
    }
    // The last occurrence wins, as for a repeated Tcl option.
    const Arg* last(std::string_view name) const {
        const Arg* r = nullptr;
        for (const auto& [k, v] : valued) {
            if (std::string_view(k) == name) r = v;
        }
        return r;
    }
};

// Unknown options are reported and skipped. A missing option value is reported and ends the scan.
Parsed parseOptions(Ctx& ctx, const Cmd& cmd, std::string_view cmdName, const std::vector<OptSpec>& specs) {
    Parsed p;
    const auto& w = cmd.words;
    for (size_t i = 1; i < w.size(); ++i) {
        const Arg& a = w[i];
        if (!isOptionWord(a)) {
            p.positional.push_back(&a);
            continue;
        }
        const auto spec = std::find_if(specs.begin(), specs.end(), [&](const OptSpec& s) { return a.value == s.name; });
        if (spec == specs.end()) {
            addProblem(ctx, "unknown option '" + shortText(a.value) + "' for " + std::string(cmdName));
            continue;
        }
        if (!spec->takesValue) {
            p.flags.push_back(a.value);
            continue;
        }
        if (i + 1 >= w.size()) {
            addProblem(ctx, "option '" + a.value + "' of " + std::string(cmdName) + " is missing its value");
            break;
        }
        p.valued.emplace_back(a.value, &w[++i]);
    }
    return p;
}

// Expands the object lists of all occurrences of the given options into `out`.
void expandOptions(Ctx& ctx, const Parsed& p, std::initializer_list<std::string_view> names,
                   std::vector<ObjectRef>& out) {
    for (std::string_view name : names) {
        for (const Arg* a : p.values(name)) {
            std::vector<ObjectRef> refs;
            expandObjects(ctx, *a, refs, 0);
            if (refs.empty()) addProblem(ctx, "option '" + std::string(name) + "' names no object");
            out.insert(out.end(), refs.begin(), refs.end());
        }
    }
}

void expandPositional(Ctx& ctx, const Parsed& p, std::vector<ObjectRef>& out) {
    for (const Arg* a : p.positional) expandObjects(ctx, *a, out, 0);
}

// Accepts plain decimal numbers only, with no trailing junk.
bool parseDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool parseInt(std::string_view s, long long& out) {
    if (!s.empty() && s.front() == '+') s.remove_prefix(1);  // from_chars does not accept '+'
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && ptr == s.data() + s.size() && !s.empty();
}

// A clock with no -name takes the name of its first source object.
std::string deriveName(const std::string& name, const std::vector<ObjectRef>& sources) {
    return name.empty() && !sources.empty() ? sources.front().pattern : name;
}

// ---------------------------------------------------------------------------------------------------------
// Command handlers.

void doCreateClock(Ctx& ctx, const Cmd& cmd) {
    const Parsed p = parseOptions(ctx, cmd, "create_clock",
                                  {{"-name", true}, {"-period", true}, {"-waveform", true}, {"-comment", true},
                                   {"-add", false}});
    ClockDef c;
    c.loc = cmd.loc;
    if (const Arg* n = p.last("-name")) c.name = n->value;
    expandPositional(ctx, p, c.sources);
    c.name = deriveName(c.name, c.sources);
    if (c.name.empty()) addProblem(ctx, "create_clock: no -name and no source object to name the clock");

    if (const Arg* per = p.last("-period")) {
        double v = 0;
        if (parseDouble(per->value, v) && v > 0) {
            c.periodNs = v;
        } else {
            addProblem(ctx, "create_clock: invalid -period '" + shortText(per->value) + "' (expected a positive number)");
        }
    } else {
        addProblem(ctx, "create_clock: missing required -period");
    }
    ctx.out.clocks.push_back(std::move(c));
}

void requirePositiveInt(Ctx& ctx, std::string_view opt, const std::string& text) {
    long long v = 0;
    if (!parseInt(text, v) || v <= 0) {
        addProblem(ctx, "create_generated_clock: invalid " + std::string(opt) + " '" + shortText(text) +
                            "' (expected a positive integer)");
    }
}

void doCreateGeneratedClock(Ctx& ctx, const Cmd& cmd) {
    const Parsed p = parseOptions(ctx, cmd, "create_generated_clock",
                                  {{"-name", true}, {"-source", true}, {"-master_clock", true},
                                   {"-divide_by", true}, {"-multiply_by", true}, {"-edges", true},
                                   {"-edge_shift", true}, {"-duty_cycle", true}, {"-phase", true},
                                   {"-comment", true}, {"-combinational", false}, {"-invert", false},
                                   {"-add", false}});
    ClockDef c;
    c.loc = cmd.loc;
    c.generated = true;
    if (const Arg* n = p.last("-name")) c.name = n->value;
    expandPositional(ctx, p, c.sources);
    c.name = deriveName(c.name, c.sources);
    if (c.sources.empty()) addProblem(ctx, "create_generated_clock: no target object");
    if (c.name.empty()) addProblem(ctx, "create_generated_clock: no -name and no target object to name the clock");

    // ClockDef keeps a single master string. -source names the pin or port where the master is defined,
    // -master_clock names the clock itself; -master_clock wins when both are given.
    std::vector<ObjectRef> srcRefs, masterRefs;
    if (!p.last("-source")) addProblem(ctx, "create_generated_clock: missing required -source");
    expandOptions(ctx, p, {"-source"}, srcRefs);
    expandOptions(ctx, p, {"-master_clock"}, masterRefs);
    for (const auto& ref : srcRefs) if (ref.kind == "all_inputs")
        addProblem(ctx, "create_generated_clock: all_inputs is not a single source object");
    for (const auto& ref : masterRefs) if (ref.kind == "all_inputs")
        addProblem(ctx, "create_generated_clock: all_inputs is a port collection, expected a master clock");
    if (!masterRefs.empty()) {
        c.masterClock = masterRefs.front().pattern;
    } else if (!srcRefs.empty()) {
        c.masterClock = srcRefs.front().pattern;
    }

    // Raw text is kept for both factors; invalid ones are also reported.
    if (const Arg* d = p.last("-divide_by")) {
        c.divideBy = d->value;
        requirePositiveInt(ctx, "-divide_by", d->value);
    }
    if (const Arg* m = p.last("-multiply_by")) {
        c.multiplyBy = m->value;
        requirePositiveInt(ctx, "-multiply_by", m->value);
    }
    if (!p.last("-divide_by") && !p.last("-multiply_by") && !p.last("-edges")) {
        addProblem(ctx, "create_generated_clock: needs -divide_by, -multiply_by or -edges");
    }
    if (const Arg* edges = p.last("-edges")) {
        const auto values = listElements(ctx, edges->value, 0);
        long long previous = 0;
        bool valid = values.size() == 3;
        for (const auto& edge : values) {
            long long n = 0;
            if (!parseInt(edge.value, n) || n <= previous) valid = false;
            previous = n;
        }
        if (!valid) addProblem(ctx, "create_generated_clock: -edges needs three increasing positive integers");
    }
    ctx.out.clocks.push_back(std::move(c));
}

void doSetClockGroups(Ctx& ctx, const Cmd& cmd) {
    const Parsed p = parseOptions(ctx, cmd, "set_clock_groups",
                                  {{"-asynchronous", false}, {"-logically_exclusive", false},
                                   {"-physically_exclusive", false}, {"-group", true}, {"-name", true},
                                   {"-comment", true}, {"-allow_paths", false}});
    ClockGroups g;
    g.loc = cmd.loc;
    for (const std::string& f : p.flags) {
        ClockGroups::Kind k = ClockGroups::Kind::Unspecified;
        if (f == "-asynchronous") k = ClockGroups::Kind::Asynchronous;
        else if (f == "-logically_exclusive") k = ClockGroups::Kind::LogicallyExclusive;
        else if (f == "-physically_exclusive") k = ClockGroups::Kind::PhysicallyExclusive;
        if (k == ClockGroups::Kind::Unspecified) continue;  // e.g. -allow_paths
        if (g.kind == ClockGroups::Kind::Unspecified) {
            g.kind = k;
        } else if (g.kind != k) {
            addProblem(ctx, "set_clock_groups: more than one of -asynchronous, -logically_exclusive, -physically_exclusive");
        }
    }
    if (g.kind == ClockGroups::Kind::Unspecified) {
        addProblem(ctx, "set_clock_groups: missing -asynchronous, -logically_exclusive or -physically_exclusive");
    }

    const auto groupArgs = p.values("-group");
    for (const Arg* a : groupArgs) {
        std::vector<ObjectRef> refs;
        expandObjects(ctx, *a, refs, 0);
        if (refs.empty()) addProblem(ctx, "set_clock_groups: empty -group list");
        for (const auto& ref : refs) if (ref.kind == "all_inputs")
            addProblem(ctx, "set_clock_groups: all_inputs is a port collection, expected clocks");
        g.groups.push_back(std::move(refs));
    }
    if (groupArgs.size() < 2) addProblem(ctx, "set_clock_groups: needs at least two -group lists");
    for (const Arg* a : p.positional) addProblem(ctx, "set_clock_groups: unexpected argument '" + shortText(a->value) + "'");
    ctx.out.clockGroups.push_back(std::move(g));
}

// Options shared by the path exceptions. -rise_from/-fall_from and friends are treated as -from/-to/-through:
// the edge restriction is not modelled, but the object lists are kept.
std::vector<OptSpec> pathOptions() {
    return {{"-from", true},          {"-to", true},          {"-through", true},
            {"-rise_from", true},     {"-fall_from", true},   {"-rise_to", true},
            {"-fall_to", true},       {"-rise_through", true}, {"-fall_through", true},
            {"-comment", true},       {"-setup", false},      {"-hold", false},
            {"-rise", false},         {"-fall", false},       {"-start", false},
            {"-end", false},          {"-datapath_only", false}, {"-reset_path", false},
            {"-ignore_clock_latency", false}};
}

PathException makePath(Ctx& ctx, const Cmd& cmd, PathException::Kind kind, const Parsed& p) {
    PathException e;
    e.kind = kind;
    e.loc = cmd.loc;
    expandOptions(ctx, p, {"-from", "-rise_from", "-fall_from"}, e.from);
    expandOptions(ctx, p, {"-to", "-rise_to", "-fall_to"}, e.to);
    expandOptions(ctx, p, {"-through", "-rise_through", "-fall_through"}, e.through);
    return e;
}

// The single positional value of a path exception (multiplier or delay). Extra words are reported.
const Arg* positionalValue(Ctx& ctx, const Parsed& p, std::string_view cmdName, std::string_view what) {
    if (p.positional.empty()) {
        addProblem(ctx, std::string(cmdName) + ": missing " + std::string(what));
        return nullptr;
    }
    for (size_t i = 1; i < p.positional.size(); ++i) {
        addProblem(ctx, std::string(cmdName) + ": unexpected argument '" + shortText(p.positional[i]->value) + "'");
    }
    return p.positional.front();
}

void doFalsePath(Ctx& ctx, const Cmd& cmd) {
    const Parsed p = parseOptions(ctx, cmd, "set_false_path", pathOptions());
    for (const Arg* a : p.positional) addProblem(ctx, "set_false_path: unexpected argument '" + shortText(a->value) + "'");
    ctx.out.exceptions.push_back(makePath(ctx, cmd, PathException::Kind::FalsePath, p));
}

void doMulticycle(Ctx& ctx, const Cmd& cmd) {
    const Parsed p = parseOptions(ctx, cmd, "set_multicycle_path", pathOptions());
    PathException e = makePath(ctx, cmd, PathException::Kind::Multicycle, p);
    if (const Arg* v = positionalValue(ctx, p, "set_multicycle_path", "multiplier")) {
        e.valueText = v->value;
        long long n = 0;
        if (!parseInt(v->value, n) || n < 0) {
            addProblem(ctx, "set_multicycle_path: invalid multiplier '" + shortText(v->value) + "'");
        }
    }
    ctx.out.exceptions.push_back(std::move(e));
}

void doDelay(Ctx& ctx, const Cmd& cmd, PathException::Kind kind) {
    const std::string_view name = kind == PathException::Kind::MaxDelay ? "set_max_delay" : "set_min_delay";
    const Parsed p = parseOptions(ctx, cmd, name, pathOptions());
    PathException e = makePath(ctx, cmd, kind, p);
    if (const Arg* v = positionalValue(ctx, p, name, "delay value")) {
        e.valueText = v->value;
        double d = 0;
        if (!parseDouble(v->value, d)) {
            addProblem(ctx, std::string(name) + ": invalid delay value '" + shortText(v->value) + "'");
        }
    }
    ctx.out.exceptions.push_back(std::move(e));
}

void doInputDelay(Ctx& ctx, const Cmd& cmd) {
    const Parsed p = parseOptions(ctx, cmd, "set_input_delay",
                                  {{"-clock", true}, {"-reference_pin", true}, {"-max", false}, {"-min", false}, {"-add_delay", false},
                                   {"-network_latency_included", false}, {"-source_latency_included", false}, {"-clock_fall", false},
                                   {"-rise", false}, {"-fall", false}});
    InputDelay d;
    d.loc = cmd.loc;
    if (const Arg* c = p.last("-clock")) {
        std::vector<ObjectRef> refs;
        expandObjects(ctx, *c, refs, 0);
        if (refs.size() != 1 || refs.front().pattern.empty()) addProblem(ctx, "set_input_delay: -clock needs one clock");
        else d.clock = refs.front().pattern;
    } else addProblem(ctx, "set_input_delay: missing -clock");
    if (p.positional.size() < 2) { addProblem(ctx, "set_input_delay: expected a delay value and a port list"); return; }
    d.valueText = p.positional[0]->value;
    double v = 0;
    if (!parseDouble(d.valueText, v)) addProblem(ctx, "set_input_delay: invalid delay value '" + shortText(d.valueText) + "'");
    for (size_t i = 1; i < p.positional.size(); ++i) expandObjects(ctx, *p.positional[i], d.ports, 0);
    if (d.ports.empty()) addProblem(ctx, "set_input_delay: empty port list");
    ctx.out.inputDelays.push_back(std::move(d));
}

// Unlike input delays, these declarations have no sound analysis consumer yet.
// Keep them distinct from unsupported commands so valid records survive atomic dispatch.
void doOutputDelay(Ctx& ctx, const Cmd& cmd) {
    const Parsed p = parseOptions(ctx, cmd, "set_output_delay", {{"-clock", true}});
    OutputDelay d;
    d.loc = cmd.loc;
    d.text = joinedText(ctx.src, cmd.begin, cmd.end);
    if (const Arg* c = p.last("-clock")) {
        std::vector<ObjectRef> refs;
        expandObjects(ctx, *c, refs, 0);
        if (refs.size() != 1 || refs.front().pattern.empty() ||
            (!refs.front().kind.empty() && refs.front().kind != "get_clocks"))
            addProblem(ctx, "set_output_delay: -clock needs one clock name");
        else d.clock = refs.front().pattern;
    } else addProblem(ctx, "set_output_delay: missing -clock");
    if (p.positional.size() < 2) {
        addProblem(ctx, "set_output_delay: expected a delay value and a port list"); return;
    }
    d.valueText = p.positional[0]->value;
    double value = 0;
    if (!parseDouble(d.valueText, value)) addProblem(ctx, "set_output_delay: invalid delay value");
    for (size_t i = 1; i < p.positional.size(); ++i) expandObjects(ctx, *p.positional[i], d.ports, 0);
    if (d.ports.empty()) addProblem(ctx, "set_output_delay: empty port list");
    for (const auto& r : d.ports)
        if (!r.kind.empty() && r.kind != "get_ports") addProblem(ctx, "set_output_delay: expected output ports");
    ctx.out.outputDelays.push_back(std::move(d));
}

void doCaseAnalysis(Ctx& ctx, const Cmd& cmd) {
    const Parsed p = parseOptions(ctx, cmd, "set_case_analysis", {});
    if (p.positional.size() < 2) {
        addProblem(ctx, "set_case_analysis: expected 0 or 1 and an object list"); return;
    }
    CaseAnalysis c;
    c.loc = cmd.loc;
    c.text = joinedText(ctx.src, cmd.begin, cmd.end);
    const auto& value = p.positional[0]->value;
    if (value != "0" && value != "1")
        addProblem(ctx, "set_case_analysis: only literal 0 or 1 is supported; value '" + shortText(value) + "' is not applied (including rising/falling)");
    else c.value = value == "1";
    for (size_t i = 1; i < p.positional.size(); ++i) expandObjects(ctx, *p.positional[i], c.objects, 0);
    if (c.objects.empty()) addProblem(ctx, "set_case_analysis: empty object list");
    for (const auto& r : c.objects)
        if (!r.kind.empty() && r.kind != "get_ports" && r.kind != "get_pins" && r.kind != "get_nets")
            addProblem(ctx, "set_case_analysis: expected ports, pins or nets");
    ctx.out.caseAnalyses.push_back(std::move(c));
}

void handle(Ctx& ctx, const Cmd& cmd) {
    ctx.cur = cmd.loc;
    const Arg& head = cmd.words.front();
    const std::string name = head.kind == ArgKind::Bracket ? "[" + head.value + "]" : head.value;
    if (name == "create_clock") doCreateClock(ctx, cmd);
    else if (name == "create_generated_clock") doCreateGeneratedClock(ctx, cmd);
    else if (name == "set_clock_groups") doSetClockGroups(ctx, cmd);
    else if (name == "set_false_path") doFalsePath(ctx, cmd);
    else if (name == "set_multicycle_path") doMulticycle(ctx, cmd);
    else if (name == "set_max_delay") doDelay(ctx, cmd, PathException::Kind::MaxDelay);
    else if (name == "set_min_delay") doDelay(ctx, cmd, PathException::Kind::MinDelay);
    else if (name == "set_input_delay") doInputDelay(ctx, cmd);
    else if (name == "set_output_delay") doOutputDelay(ctx, cmd);
    else if (name == "set_case_analysis") doCaseAnalysis(ctx, cmd);
    else ctx.out.unsupported.push_back(Unsupported{name, joinedText(ctx.src, cmd.begin, cmd.end), cmd.loc});
}

// Variable substitution is single pass: substituted values are never evaluated again.
// Arrays, embedded command substitutions and nontrivial backslash escapes are disclosed.
bool substitute(Ctx& ctx, Arg& a, int depth = 0) {
    if (depth > kMaxDepth) { addProblem(ctx, "substitution nested too deeply"); return false; }
    if (a.kind == ArgKind::Brace) return true;
    if (a.kind == ArgKind::Bracket) {
        for (auto& word : a.sub) if (!substitute(ctx, word, depth + 1)) return false;
        if (a.sub.empty()) { addProblem(ctx, "empty command substitution"); return false; }
        if (isObjectCommand(a.sub[0].value) || a.sub[0].value == "all_clocks" || a.sub[0].value == "all_inputs") return true;
        // [set name] is the read form only. Assignment substitutions are disclosed.
        if (a.sub[0].value == "set" && a.sub.size() == 2) {
            auto it = ctx.variables.find(a.sub[1].value);
            if (it != ctx.variables.end()) { a = it->second; return true; }
            addProblem(ctx, "undefined variable '" + shortText(a.sub[1].value) + "'");
        } else addProblem(ctx, "unsupported object command '" + shortText(a.sub[0].value) + "' (expected get_ports, get_pins, get_nets, get_clocks, get_cells or all_clocks)");
        return false;
    }
    const std::string raw = a.value;
    std::string value;
    for (size_t i = 0; i < raw.size();) {
        if (raw[i] == '\\') {
            if (i + 1 == raw.size()) { value += raw[i++]; continue; }
            char c = raw[i + 1];
            if (c == '\n') {
                value += ' '; i += 2;
                while (i < raw.size() && isHSpace(raw[i])) ++i;
            } else if (c == '$' || c == '[' || c == ']' || c == '{' || c == '}' || c == '"' ||
                       c == '\\' || c == ' ' || c == ';') { value += c; i += 2; }
            else { addProblem(ctx, "unsupported backslash substitution"); return false; }
        } else if (raw[i] == '[') {
            addProblem(ctx, "embedded command substitution is not interpreted"); return false;
        } else if (raw[i] == '$') {
            const size_t dollar = i;
            size_t start = ++i;
            std::string name;
            if (i < raw.size() && raw[i] == '{') {
                start = ++i;
                while (i < raw.size() && raw[i] != '}') ++i;
                if (i == raw.size()) { addProblem(ctx, "unbalanced variable name"); return false; }
                name = raw.substr(start, i++ - start);
            } else {
                while (i < raw.size() && (std::isalnum(static_cast<unsigned char>(raw[i])) || raw[i] == '_')) ++i;
                name = raw.substr(start, i - start);
                if (i < raw.size() && (raw[i] == '(' || raw[i] == ':')) {
                    addProblem(ctx, "array or namespace variable substitution is not interpreted"); return false;
                }
                if (name.empty()) { value += '$'; continue; }
            }
            auto it = ctx.variables.find(name);
            if (it == ctx.variables.end()) { addProblem(ctx, "undefined variable '" + shortText(name) + "'"); return false; }
            if (it->second.kind == ArgKind::Bracket) {
                if (dollar == 0 && i == raw.size()) { a = it->second; return true; }
                addProblem(ctx, "embedded object collection substitution is not interpreted"); return false;
            }
            value += it->second.value;
        } else value += raw[i++];
    }
    a.value = std::move(value);
    return true;
}

void dispatch(Ctx& ctx, const Cmd& original, int depth = 0) {
    ctx.cur = original.loc;
    if (!original.valid) { ctx.variables.clear(); return; }
    if (depth > kMaxDepth) { addProblem(ctx, "script nested too deeply"); return; }
    Cmd cmd = original;
    const size_t problems = ctx.out.problems.size();
    const size_t unsupported = ctx.out.unsupported.size();
    const size_t clocks = ctx.out.clocks.size(), groups = ctx.out.clockGroups.size();
    const size_t exceptions = ctx.out.exceptions.size(), delays = ctx.out.inputDelays.size();
    const size_t outputs = ctx.out.outputDelays.size(), cases = ctx.out.caseAnalyses.size();
    for (auto& a : cmd.words) if (!substitute(ctx, a)) {
        // Continuing after an unevaluated script must not reuse stale variable values.
        ctx.variables.clear();
        return;
    }
    const auto& w = cmd.words;
    const std::string name = w.front().value;
    bool sourceEvaluated = false;
    if (name == "set") {
        if (w.size() != 2 && w.size() != 3) addProblem(ctx, "set: expected name and optional value");
        else if (w[1].value.empty() || w[1].value.find_first_of("(:") != std::string::npos)
            addProblem(ctx, "set: array or namespace variables are not interpreted");
        else if (w.size() == 3) ctx.variables[w[1].value] = w[2];
        else if (!ctx.variables.count(w[1].value)) addProblem(ctx, "undefined variable '" + shortText(w[1].value) + "'");
    } else if (name == "source") {
        if (w.size() != 2 || w[1].value.empty() || w[1].value[0] == '-') {
            addProblem(ctx, "source: expected one file path; options are unsupported");
        } else {
            namespace fs = std::filesystem;
            std::error_code ec;
            auto path = fs::path(w[1].value);
            if (path.is_relative()) path = fs::path(ctx.file).parent_path() / path;
            path = fs::weakly_canonical(path, ec);
            if (ec) addProblem(ctx, "source: cannot resolve file");
            else if (ctx.sourceStack.size() >= 64) addProblem(ctx, "source: depth limit exceeded");
            else if (std::find(ctx.sourceStack.begin(), ctx.sourceStack.end(), path) != ctx.sourceStack.end())
                addProblem(ctx, "source: cycle detected");
            else {
                std::ifstream input(path, std::ios::binary);
                if (!input) addProblem(ctx, "source: file could not be read");
                else {
                    std::ostringstream buffer; buffer << input.rdbuf();
                    if (input.bad()) addProblem(ctx, "source: file could not be read");
                    else {
                        sourceEvaluated = true;
                        const auto savedSrc = ctx.src; const auto savedFile = ctx.file;
                        const auto savedBase = ctx.base; const auto savedCur = ctx.cur;
                        std::string text = buffer.str();
                        ctx.out.sourcedFiles.push_back(path.string());
                        ctx.sourceStack.push_back(path);
                        ctx.src = text; ctx.file = path.string(); ctx.base = SourceLocation{ctx.file, 0, 0};
                        std::vector<size_t> starts{0};
                        for (size_t i = 0; i < text.size(); ++i) if (text[i] == '\n') starts.push_back(i + 1);
                        Scanner scanner(ctx.src, ctx, &starts, 0);
                        for (const auto& child : scanner.parseScript(false)) dispatch(ctx, child, depth + 1);
                        ctx.sourceStack.pop_back(); ctx.src = savedSrc; ctx.file = savedFile;
                        ctx.base = savedBase; ctx.cur = savedCur;
                    }
                }
            }
        }
    } else if (name == "foreach") {
        if (w.size() != 4 || original.words[3].kind != ArgKind::Brace) {
            addProblem(ctx, "foreach: only one variable, one list and a braced body are interpreted");
        } else {
            auto vars = listElements(ctx, w[1].value, 0);
            std::vector<Arg> items;
            if (w[2].kind == ArgKind::Bracket) {
                std::vector<ObjectRef> refs;
                expandObjects(ctx, w[2], refs, 0);
                for (const auto& ref : refs) { Arg item; item.value = ref.pattern; items.push_back(std::move(item)); }
            } else items = listElements(ctx, w[2].value, 0);
            if (vars.size() != 1 || vars[0].value.empty() || vars[0].value.find_first_of("(:") != std::string::npos)
                addProblem(ctx, "foreach: expected one scalar variable");
            else if (items.size() > 1000 || ctx.iterations + items.size() > 10000)
                addProblem(ctx, "foreach: iteration limit exceeded (1000 per loop, 10000 per parse)");
            if (ctx.out.problems.size() == problems) {
                // Body offsets are relative to the current script, never the substituted value.
                const auto savedSrc = ctx.src;
                const auto savedBase = ctx.base;
                SourceLocation base = ctx.base;
                for (size_t i = 0; i < w[3].content; ++i) {
                    if (ctx.src[i] == '\n') { ++base.line; base.column = 0; }
                    else ++base.column;
                }
                ctx.base = base;
                ctx.src = w[3].script;
                std::vector<size_t> starts{0};
                for (size_t i = 0; i < ctx.src.size(); ++i) if (ctx.src[i] == '\n') starts.push_back(i + 1);
                Scanner scanner(ctx.src, ctx, &starts, depth + 1);
                auto body = scanner.parseScript(false);
                for (auto item : items) {
                    ++ctx.iterations;
                    // List parsing does not perform substitutions, including bracket-looking elements.
                    item.kind = ArgKind::Brace;
                    ctx.variables[vars[0].value] = std::move(item);
                    for (const auto& child : body) dispatch(ctx, child, depth + 1);
                    if (ctx.out.problems.size() != problems || ctx.out.unsupported.size() != unsupported) break;
                }
                ctx.src = savedSrc; ctx.base = savedBase;
            }
        }
    } else handle(ctx, cmd);
    if (ctx.out.problems.size() != problems || ctx.out.unsupported.size() != unsupported) {
        // A sourced script has the same per-command rejection as inline text.
        // Diagnostics in a child must not roll back its valid neighbours.
        if (name == "source") {
            if (!sourceEvaluated) ctx.variables.clear();
            return;
        }
        if (name == "set" || name == "foreach" || ctx.out.unsupported.size() != unsupported)
            ctx.variables.clear();
        ctx.out.clocks.resize(clocks); ctx.out.clockGroups.resize(groups);
        ctx.out.exceptions.resize(exceptions); ctx.out.inputDelays.resize(delays);
        ctx.out.outputDelays.resize(outputs); ctx.out.caseAnalyses.resize(cases);
    }
}

} // namespace

Constraints parse(std::string_view text, const std::string& fileName) {
    Constraints out;
    try {
        Ctx ctx{out, text, fileName, SourceLocation{fileName, 0, 0}, {}, {}, 0};
        std::error_code ec;
        auto rootPath = std::filesystem::weakly_canonical(fileName, ec);
        if (!ec) ctx.sourceStack.push_back(rootPath);
        std::vector<size_t> lineStarts{size_t{0}};
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') lineStarts.push_back(i + 1);
        }
        Scanner top(text, ctx, &lineStarts, 0);
        for (const Cmd& cmd : top.parseScript(false)) dispatch(ctx, cmd);

        // Tokenising and interpreting run in separate passes; sort problems back into source order.
        std::stable_sort(out.problems.begin(), out.problems.end(), [](const ParseProblem& a, const ParseProblem& b) {
            if (a.loc.line != b.loc.line) return a.loc.line < b.loc.line;
            return a.loc.column < b.loc.column;
        });
    } catch (...) {
        // Parsing must never throw (e.g. on allocation failure). Keep whatever was parsed so far.
        out.problems.push_back({"internal error while parsing SDC; results may be incomplete",
                                SourceLocation{fileName, 0, 0}});
    }
    return out;
}

} // namespace sverity::sdc
