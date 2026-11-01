// SDC (Tcl subset) parser.
//
// Two stages. A Tcl-like tokenizer (Scanner) splits the text into commands, each a list of words: bare,
// {braced}, "quoted" or [bracketed]. Then each interpreted command is decoded from its words. Nothing is
// substituted: "$x" and "[expr ...]" stay literal text, and the handlers report them as malformed where a
// number or an object is required. Every command that is not interpreted goes to `unsupported`.
#include "sverity/sdc.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sverity::sdc {

void Constraints::merge(Constraints&& o) {
    auto mv = [](auto& dst, auto& src) { for (auto& x : src) dst.push_back(std::move(x)); };
    mv(clocks, o.clocks); mv(clockGroups, o.clockGroups); mv(exceptions, o.exceptions);
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
    std::vector<Arg> sub;  // Bracket only: the words of the command inside the brackets
};

struct Cmd {
    std::vector<Arg> words;
    size_t begin = 0, end = 0;  // extent in the top-level text; used to rebuild `unsupported` text
    SourceLocation loc;
};

// State shared by one parse() call.
struct Ctx {
    Constraints& out;
    std::string_view src;  // whole input
    std::string file;
    SourceLocation cur;    // command being tokenised or interpreted; problems are reported here
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
    Arg parseQuoted();
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
        WordsEnd we = scanWords(cmd.words, false, inBracket);
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
    Arg a;
    switch (src_[pos_]) {
    case '{': a = parseBraced(); break;
    case '"': a = parseQuoted(); break;
    case '[': a = parseBracketWord(); break;
    default: return parseBare(listMode, inBracket);
    }
    // Tcl needs a separator right after a braced, quoted or bracketed word. Skip any junk and report it.
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
    return a;
}

// "...": the raw text between the quotes. Bracketed substitutions inside are skipped as a unit so that a
// quote inside them does not end the word.
Arg Scanner::parseQuoted() {
    const size_t n = src_.size();
    size_t p = pos_ + 1;
    Arg a;
    a.kind = ArgKind::Quoted;
    bool closed = false;
    while (p < n) {
        const char c = src_[p];
        if (c == '\\' && p + 1 < n) {
            a.value.append(src_.substr(p, 2));
            p += 2;
        } else if (c == '"') {
            closed = true;
            break;
        } else if (c == '[') {
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
        if (isHSpace(c) || c == '\n') break;
        if (c == ';' && !listMode) break;
        if (c == ']' && inBracket) break;
        if (contLen(src_, p)) break;
        if (c == '\\') {
            const size_t len = (p + 1 < n) ? 2 : 1;
            a.value.append(src_.substr(p, len));
            p += len;
        } else if (c == '[') {
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
    return SourceLocation{ctx_.file, static_cast<uint32_t>(line), static_cast<uint32_t>(pos - lineStart + 1)};
}

// Parses `text` as a Tcl list (used for the words of a brace group or a quoted word).
std::vector<Arg> listElements(Ctx& ctx, std::string_view text, int depth) {
    std::vector<Arg> words;
    Scanner s(text, ctx, nullptr, depth);
    s.scanWords(words, /*listMode=*/true, /*inBracket=*/false);
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
    return a.kind == ArgKind::Bare && a.value.size() >= 2 && a.value[0] == '-' &&
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
        for (const ObjectRef& r : names) out.push_back(ObjectRef{cmdName, r.pattern});
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

void dispatch(Ctx& ctx, const Cmd& cmd) {
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
    else ctx.out.unsupported.push_back(Unsupported{name, joinedText(ctx.src, cmd.begin, cmd.end), cmd.loc});
}

} // namespace

Constraints parse(std::string_view text, const std::string& fileName) {
    Constraints out;
    try {
        Ctx ctx{out, text, fileName, SourceLocation{fileName, 0, 0}};
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
