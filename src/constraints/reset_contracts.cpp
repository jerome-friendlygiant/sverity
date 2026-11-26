// Schema parser and normalizer for the `reset_contracts` configuration section.
#include "sverity/reset_contracts.h"

#include <set>

namespace sverity {
namespace {

using json::Value;

bool fail(std::string& error, const std::string& message) {
    error = "reset_contracts: " + message;
    return false;
}

/// Reads a required nonempty string field.
bool requiredString(const Value& item, const char* key, const std::string& where, std::string& out, std::string& error) {
    const Value* value = item.find(key);
    if (!value || !value->isString() || value->asString().empty())
        return fail(error, where + "." + key + " must be a nonempty string");
    out = value->asString();
    return true;
}

/// Rejects any field of `item` outside `allowed`.
bool checkFields(const Value& item, const std::set<std::string>& allowed, const std::string& where, std::string& error) {
    for (const auto& [key, value] : item.asObject())
        if (!allowed.count(key)) return fail(error, "unknown field '" + key + "' in " + where);
    return true;
}

/// Reads a clause or source array; absent means empty. Every entry must be an object.
bool entries(const Value& section, const char* key, std::vector<const Value*>& out, std::string& error) {
    const Value* array = section.find(key);
    if (!array) return true;
    if (!array->isArray()) return fail(error, std::string(key) + " must be an array");
    for (const auto& item : array->asArray()) {
        if (!item.isObject()) return fail(error, std::string(key) + " entries must be objects");
        out.push_back(&item);
    }
    return true;
}

bool isSelectorSafe(const std::string& name) {
    for (char c : name) {
        if (std::string("*?{}").find(c) != std::string::npos) return false;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') return false;
    }
    return true;
}

} // namespace

bool parseResetContracts(const Value& section, ResetContracts& out, std::string& error) {
    if (!section.isObject()) return fail(error, "section must be an object");
    static const std::set<std::string> topKeys = {"sources", "assert_together", "release_order", "state"};
    for (const auto& [key, value] : section.asObject())
        if (!topKeys.count(key)) return fail(error, "unknown key '" + key + "'");

    std::vector<const Value*> sourceItems, groupItems, orderItems, stateItems;
    if (!entries(section, "sources", sourceItems, error)) return false;
    if (!entries(section, "assert_together", groupItems, error)) return false;
    if (!entries(section, "release_order", orderItems, error)) return false;
    if (!entries(section, "state", stateItems, error)) return false;
    if (sourceItems.empty()) return fail(error, "sources must be a nonempty array");
    if (groupItems.empty() && orderItems.empty() && stateItems.empty())
        return fail(error, "at least one of assert_together, release_order or state is required");

    ResetContracts parsed;
    std::set<std::string> ids;
    auto uniqueId = [&](const std::string& id) {
        return ids.insert(id).second ? true : fail(error, "duplicate id '" + id + "'");
    };
    auto knownSource = [&](const std::string& id) {
        for (const auto& s : parsed.sources) if (s.id == id) return true;
        return fail(error, "unknown source id '" + id + "'");
    };

    for (const Value* item : sourceItems) {
        if (!checkFields(*item, {"id", "port", "root", "active"}, "source", error)) return false;
        ResetContracts::Source s;
        if (!requiredString(*item, "id", "source", s.id, error) || !uniqueId(s.id)) return false;
        const Value* port = item->find("port");
        const Value* root = item->find("root");
        if ((port != nullptr) == (root != nullptr)) return fail(error, "source '" + s.id + "' needs exactly one of port or root");
        if (port && (!port->isString() || port->asString().empty())) return fail(error, "source '" + s.id + "'.port must be a nonempty string");
        if (root && (!root->isString() || root->asString().empty())) return fail(error, "source '" + s.id + "'.root must be a nonempty string");
        if (port) s.port = port->asString();
        if (root) s.root = root->asString();
        if (!s.port.empty() && !isSelectorSafe(s.port)) return fail(error, "source '" + s.id + "'.port must name one exact name without wildcards or whitespace");
        if (!s.root.empty() && !isSelectorSafe(s.root)) return fail(error, "source '" + s.id + "'.root must name one exact name without wildcards or whitespace");
        if (!requiredString(*item, "active", "source '" + s.id + "'", s.active, error)) return false;
        if (s.active != "low" && s.active != "high") return fail(error, "source '" + s.id + "'.active must be low or high");
        parsed.sources.push_back(std::move(s));
    }

    for (const Value* item : groupItems) {
        if (!checkFields(*item, {"id", "sources"}, "assert_together", error)) return false;
        ResetContracts::Group g;
        if (!requiredString(*item, "id", "assert_together", g.id, error) || !uniqueId(g.id)) return false;
        const Value* members = item->find("sources");
        if (!members || !members->isArray()) return fail(error, "assert_together '" + g.id + "'.sources must be an array");
        for (const auto& m : members->asArray()) {
            if (!m.isString() || m.asString().empty()) return fail(error, "assert_together '" + g.id + "'.sources must contain nonempty strings");
            for (const auto& prior : g.sources) if (prior == m.asString()) return fail(error, "assert_together '" + g.id + "' repeats source '" + prior + "'");
            if (!knownSource(m.asString())) return false;
            g.sources.push_back(m.asString());
        }
        if (g.sources.size() < 2) return fail(error, "assert_together '" + g.id + "' needs at least two sources");
        parsed.assertTogether.push_back(std::move(g));
    }

    for (const Value* item : orderItems) {
        if (!checkFields(*item, {"id", "before", "after", "clock", "edge", "min_cycles"}, "release_order", error)) return false;
        ResetContracts::Order o;
        if (!requiredString(*item, "id", "release_order", o.id, error) || !uniqueId(o.id)) return false;
        const std::string where = "release_order '" + o.id + "'";
        if (!requiredString(*item, "before", where, o.before, error) || !knownSource(o.before)) return false;
        if (!requiredString(*item, "after", where, o.after, error) || !knownSource(o.after)) return false;
        if (!requiredString(*item, "clock", where, o.clock, error)) return false;
        if (!requiredString(*item, "edge", where, o.edge, error)) return false;
        if (o.edge != "posedge" && o.edge != "negedge") return fail(error, where + ".edge must be posedge or negedge");
        if (o.before == o.after) return fail(error, where + " has before equal to after");
        const Value* cycles = item->find("min_cycles");
        if (!cycles || !cycles->isInt() || cycles->asInt() < 1 || cycles->asInt() > 2147483647)
            return fail(error, where + ".min_cycles is required and must be a JSON integer from 1 to 2147483647");
        o.minCycles = cycles->asInt();
        parsed.releaseOrder.push_back(std::move(o));
    }

    for (const Value* item : stateItems) {
        if (!checkFields(*item, {"id", "source", "initial"}, "state", error)) return false;
        ResetContracts::State st;
        if (!requiredString(*item, "id", "state", st.id, error) || !uniqueId(st.id)) return false;
        const std::string where = "state '" + st.id + "'";
        if (!requiredString(*item, "source", where, st.source, error) || !knownSource(st.source)) return false;
        if (!requiredString(*item, "initial", where, st.initial, error)) return false;
        if (st.initial != "asserted") return fail(error, where + ".initial must be asserted");
        parsed.state.push_back(std::move(st));
    }

    parsed.present = true;
    out = std::move(parsed);
    return true;
}

json::Value toJson(const ResetContracts& contracts) {
    Value root = Value::object();
    Value sources = Value::array();
    for (const auto& s : contracts.sources) {
        Value item = Value::object();
        item.set("id", s.id);
        if (!s.port.empty()) item.set("port", s.port);
        if (!s.root.empty()) item.set("root", s.root);
        item.set("active", s.active);
        sources.push(std::move(item));
    }
    root.set("sources", std::move(sources));
    if (!contracts.assertTogether.empty()) {
        Value groups = Value::array();
        for (const auto& g : contracts.assertTogether) {
            Value members = Value::array();
            for (const auto& m : g.sources) members.push(m);
            Value item = Value::object();
            item.set("id", g.id).set("sources", std::move(members));
            groups.push(std::move(item));
        }
        root.set("assert_together", std::move(groups));
    }
    if (!contracts.releaseOrder.empty()) {
        Value orders = Value::array();
        for (const auto& o : contracts.releaseOrder) {
            Value item = Value::object();
            item.set("id", o.id).set("before", o.before).set("after", o.after).set("clock", o.clock)
                .set("edge", o.edge).set("min_cycles", o.minCycles);
            orders.push(std::move(item));
        }
        root.set("release_order", std::move(orders));
    }
    if (!contracts.state.empty()) {
        Value states = Value::array();
        for (const auto& st : contracts.state) {
            Value item = Value::object();
            item.set("id", st.id).set("source", st.source).set("initial", st.initial);
            states.push(std::move(item));
        }
        root.set("state", std::move(states));
    }
    return root;
}

} // namespace sverity
