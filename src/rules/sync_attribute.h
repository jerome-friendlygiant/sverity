#pragma once
// The synchronisation attributes CDC-012 and RDC-030 read, in one place. `(* ASYNC_REG = "FALSE" *)` asks for the opposite.
#include <algorithm>
#include <cctype>
#include <string>

#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/AttributeSymbol.h"
#include "slang/ast/symbols/ValueSymbol.h"

namespace sverity {

inline bool hasSyncAttribute(const slang::ast::Compilation& compilation, const slang::ast::ValueSymbol& symbol) {
    const auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    for (const auto* attribute : const_cast<slang::ast::Compilation&>(compilation).getAttributes(symbol)) {
        const std::string name = lower(std::string(attribute->name));
        if (name != "async_reg" && name != "sverity_sync" && name != "cdc_sync") continue;
        const auto& value = attribute->getValue();
        if (value.isInteger() && value.integer() == 0) return false;
        // A string literal in an attribute evaluates to its bits, so read it back as text.
        const slang::ConstantValue asText = value.convertToStr();
        if (asText.isString()) {
            const std::string text = lower(asText.str());
            if (text == "false" || text == "no" || text == "0") return false;
        }
        return true;
    }
    return false;
}

} // namespace sverity
