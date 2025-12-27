/*
 * C++20 Compile-Time AND Runtime LISP INTERPRETER
 *
 * Implements a minimal Lisp (like McCarthy's)
 * that is parsed and evaluated entirely at compile-time.
 * This version uses C++20 idioms like std::span,
 * std::visit, and std::transform_reduce.
 *
 * Build Commands:
 *   make              - Standard build (39KB, portable)
 *   make small        - Size-optimized (18-22KB, portable)
 *   make ultra-small  - Minimal build (6-8KB, POSIX-only)
 */

#include <string_view>
#include <cstddef>
#include <algorithm> // for std::copy
#include <vector>    // for std::vector (constexpr in C++20)
#include <variant>   // for std::variant (for Atom)
#include <stdexcept> // for std::runtime_error
#include <span>      // for std::span (C++20)
#include <numeric>   // for std::transform_reduce (constexpr in C++20)
#include <functional>  // for std::plus/multiplies
#include <optional>  // for std::optional (constexpr-friendly)
#include <list>      // for std::list (stable references)

// Conditional includes based on build mode
#ifndef MINIMAL_BUILD
#include <iostream>  // For std::cout (standard build)
#include <string>    // For std::string and std::getline
#else
// POSIX I/O for minimal build
#include <unistd.h>  // For write, read
#include <cstring>   // For strlen, memset
#endif

// 1. A struct that can hold a string at compile-time
// (Allowed as a template parameter in C++20)
template <size_t N>
struct FixedString {
    char data[N];
    consteval FixedString(const char (&str)[N]) {
        std::copy(str, str + N, data);
    }
    constexpr std::string_view get() const {
        return std::string_view(data, N - 1);
    }
};

// --- Start of Core Lisp Interpreter ---
// This logic is all `constexpr`, so it can be
// used at both compile-time and runtime.

namespace MiniLisp {

// =============================================================================
// SYMBOL INTERNING
// =============================================================================
// Problem: In WASM, the input buffer at offset 1024 gets reused between calls.
// string_view pointers into this buffer become invalid after the next eval.
//
// Solution: Store all symbol strings in a global table (interning). The parser
// returns string_views that point into this permanent table, not the input buffer.
// This way all symbol references remain valid across multiple eval calls.
//
// This is the standard approach used by real Lisp implementations:
// - Emacs Lisp uses an "obarray" (hash table of symbols)
// - Common Lisp interns symbols in packages
// - Scheme implementations typically use a symbol table
//
// Benefits:
// 1. No lifetime issues - symbols persist for program duration
// 2. Fast comparison - comparing string_views (pointer+len) is fast
// 3. Memory efficient - each unique symbol stored once
// 4. Safe copying - Lambda/Env can be copied freely (just string_views)
// =============================================================================

struct SymbolTable {
    // IMPORTANT: Use std::list, NOT std::vector!
    // When vector grows, it reallocates and moves strings. With SSO (Small String
    // Optimization), short strings store their data inside the string object.
    // Moving the string moves the data, invalidating string_views pointing to it.
    // std::list elements never move, so string_views into them remain valid forever.
    std::list<std::string> symbols;

    // WASM string comparison workaround - explicit character comparison
    static bool str_equals(const std::string& a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++) {
            if (a[i] != b[i]) return false;
        }
        return true;
    }

    // Intern a symbol - returns string_view into permanent storage
    std::string_view intern(std::string_view s) {
        // Check if already interned (use explicit char comparison for WASM)
        for (const auto& sym : symbols) {
            if (str_equals(sym, s)) {
                return std::string_view(sym);
            }
        }
        // Add new symbol - list doesn't invalidate references when adding
        symbols.push_back(std::string(s));
        return std::string_view(symbols.back());
    }

    void clear() { symbols.clear(); }
    size_t size() const { return symbols.size(); }
};

// Lazy initialization for WASM compatibility (avoids static init order issues)
inline SymbolTable* get_symbol_table() {
    static SymbolTable table;
    return &table;
}

// --- 1. AST (Abstract Syntax Tree) Data Structures ---

struct SExpr; // Forward declaration

// An "Atom" is either a number (long) or a symbol (string_view)
// For runtime/WASM: string_views point into the global SymbolTable
// For compile-time: string_views point into the source literal
using Atom = std::variant<long, std::string_view>;

// A "List" is a vector of other S-Expressions
using List = std::vector<SExpr>;

// An S-Expression is the main data type: either an Atom or a List
struct SExpr {
    std::optional<Atom> atom;
    std::optional<List> list;

    // Constexpr constructors for ease of use
    constexpr SExpr(Atom a) : atom(std::move(a)), list(std::nullopt) {}
    constexpr SExpr(List l) : atom(std::nullopt), list(std::move(l)) {}
};

// A Lambda stores parameter names and body expression
// With interning, all string_views point to the global SymbolTable,
// so Lambda can be safely copied without lifetime issues.
struct Lambda {
    std::vector<std::string_view> params;  // Point into SymbolTable
    List body;                              // Contains interned symbols

    Lambda(std::vector<std::string_view> p, const SExpr& b)
        : params(std::move(p)) {
        // Body's symbols are already interned by parse_interned()
        if (b.list.has_value()) {
            body = *b.list;
        } else {
            body.push_back(b);
        }
    }

    SExpr get_body() const {
        if (body.size() == 1 && body[0].atom.has_value()) {
            return body[0];
        }
        return SExpr{body};
    }

    std::string_view get_param(size_t i) const {
        return params[i];
    }
};

// Global function storage - separate from Env to avoid copy issues
struct FunctionStore {
    std::vector<std::pair<std::string_view, Lambda>> functions;  // Names are interned

    const Lambda* lookup(std::string_view name) const {
        for (auto it = functions.rbegin(); it != functions.rend(); ++it) {
            if (it->first == name) return &it->second;
        }
        return nullptr;
    }

    void define(std::string_view name, Lambda fn) {
        // Remove existing definition with same name
        functions.erase(
            std::remove_if(functions.begin(), functions.end(),
                [&name](const auto& p) { return p.first == name; }),
            functions.end()
        );
        // Name should already be interned by caller
        functions.push_back({name, std::move(fn)});
    }

    void clear() { functions.clear(); }
    size_t size() const { return functions.size(); }
};

// Environment for variable bindings only (can be safely copied)
struct Env {
    std::vector<std::pair<std::string_view, SExpr>> bindings;
    FunctionStore* fn_store;  // Pointer to shared function store

    Env(FunctionStore* store) : fn_store(store) {}

    const SExpr* lookup(std::string_view name) const {
        for (auto it = bindings.rbegin(); it != bindings.rend(); ++it) {
            if (it->first == name) return &it->second;
        }
        return nullptr;
    }

    const Lambda* lookup_fn(std::string_view name) const {
        return fn_store ? fn_store->lookup(name) : nullptr;
    }

    void define(std::string_view name, SExpr value) {
        bindings.push_back({name, std::move(value)});
    }

    void define_fn(std::string_view name, Lambda fn) {
        if (fn_store) fn_store->define(name, std::move(fn));
    }

    void clear() {
        bindings.clear();
        if (fn_store) fn_store->clear();
    }
};


// --- 2. Parser (String -> AST) ---

// Helper to throw errors (can be used at compile-time or runtime)
// In C++20, throw in constexpr is allowed as long as it's not executed at compile-time
constexpr void p_assert(bool cond, [[maybe_unused]] const char* msg) {
    if (!cond) {
#ifdef WASM_BUILD
        __builtin_trap();
#else
        throw std::runtime_error(msg);
#endif
    }
}

// Helper to skip whitespace
constexpr void skip_ws(std::string_view& s) {
    while (!s.empty() && (s[0] == ' ' || s[0] == '\n' || s[0] == '\t')) {
        s.remove_prefix(1);
    }
}

// Forward-declare main parse function
constexpr SExpr parse(std::string_view& s);

// Simple string-to-long
constexpr long s_to_l(std::string_view s) {
    long res = 0;
    bool neg = false;
    if (!s.empty() && s[0] == '-') {
        neg = true;
        s.remove_prefix(1);
    }
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            res = res * 10 + (c - '0');
        } else {
            p_assert(false, "Invalid number");
        }
    }
    return neg ? -res : res;
}

// Parses an atom (number or symbol)
constexpr Atom parse_atom(std::string_view& s) {
    size_t len = 0;
    while (len < s.size() && s[len] != ' ' && s[len] != ')' && s[len] != '\'' && s[len] != '\n' && s[len] != '\t') {
        len++;
    }
    p_assert(len > 0, "Empty atom");
    auto val = s.substr(0, len);
    s.remove_prefix(len);

    // Try to parse as a number
    bool is_num = !val.empty();
    size_t start_idx = 0;

    if (val[0] == '-') {
        if (val.size() == 1) { // Just "-", that's a symbol
            is_num = false;
        } else {
            start_idx = 1; // Check digits from index 1
        }
    } else if (val[0] < '0' || val[0] > '9') {
        is_num = false; // Doesn't start with a digit or '-', so not a number
    }
    
    // If we still think it's a number, check all relevant chars
    if (is_num) {
        if (val.size() == start_idx) { // e.g., it was just "-"
             is_num = false;
        } else {
            for(size_t i = start_idx; i < val.size(); ++i) {
                if (val[i] < '0' || val[i] > '9') {
                    is_num = false; // Found a non-digit
                    break;
                }
            }
        }
    }
    
    if (is_num) {
        return s_to_l(val);
    }
    
    // Otherwise, it's a symbol
    return val;
}

// Parses a list: (op arg1 arg2 ...)
constexpr List parse_list(std::string_view& s) {
    s.remove_prefix(1); // Eat '('
    List list;
    while (true) {
        skip_ws(s);
        p_assert(!s.empty(), "Unterminated list");
        if (s[0] == ')') {
            s.remove_prefix(1); // Eat ')'
            return list;
        }
        list.push_back(parse(s));
    }
}

// Main parse function
constexpr SExpr parse(std::string_view& s) {
    skip_ws(s);
    p_assert(!s.empty(), "Unexpected end of input");

    // Handle ' (quote) sugar
    if (s[0] == '\'') {
        s.remove_prefix(1); // Eat '
        List quote_list;
        quote_list.push_back(SExpr{Atom{"quote"}}); // (quote ...)
        quote_list.push_back(parse(s));           // (... thing-to-quote)
        return SExpr{quote_list};
    }

    if (s[0] == '(') {
        // This will call the SExpr(List l) constructor
        return SExpr{parse_list(s)};
    } else {
        // This will call the SExpr(Atom a) constructor
        return SExpr{parse_atom(s)};
    }
}

// =============================================================================
// RUNTIME PARSER WITH INTERNING
// =============================================================================
// This parser is used for WASM and runtime evaluation. It interns all symbols
// into the global SymbolTable, ensuring string_views remain valid forever.
// The constexpr parser above is used for compile-time evaluation where
// string_views point into compile-time string literals (always valid).
// =============================================================================

// Forward declarations for interning parser
SExpr parse_interned(std::string_view& s);

// Parse atom with interning - symbols go into the global table
Atom parse_atom_interned(std::string_view& s) {
    size_t len = 0;
    while (len < s.size() && s[len] != ' ' && s[len] != ')' &&
           s[len] != '\'' && s[len] != '\n' && s[len] != '\t') {
        len++;
    }
    p_assert(len > 0, "Empty atom");
    auto val = s.substr(0, len);
    s.remove_prefix(len);

    // Check if it's a number
    bool is_num = !val.empty();
    size_t start_idx = 0;

    if (val[0] == '-') {
        if (val.size() == 1) {
            is_num = false;
        } else {
            start_idx = 1;
        }
    } else if (val[0] < '0' || val[0] > '9') {
        is_num = false;
    }

    if (is_num) {
        if (val.size() == start_idx) {
            is_num = false;
        } else {
            for (size_t i = start_idx; i < val.size(); ++i) {
                if (val[i] < '0' || val[i] > '9') {
                    is_num = false;
                    break;
                }
            }
        }
    }

    if (is_num) {
        return s_to_l(val);
    }

    // INTERN the symbol - this is the key difference from constexpr parse
    return get_symbol_table()->intern(val);
}

// Parse list with interning
List parse_list_interned(std::string_view& s) {
    s.remove_prefix(1); // Eat '('
    List list;
    while (true) {
        skip_ws(s);
        p_assert(!s.empty(), "Unterminated list");
        if (s[0] == ')') {
            s.remove_prefix(1); // Eat ')'
            return list;
        }
        list.push_back(parse_interned(s));
    }
}

// Main interning parse function
SExpr parse_interned(std::string_view& s) {
    skip_ws(s);
    p_assert(!s.empty(), "Unexpected end of input");

    // Handle ' (quote) sugar
    if (s[0] == '\'') {
        s.remove_prefix(1); // Eat '
        List quote_list;
        // Intern "quote" symbol
        quote_list.push_back(SExpr{Atom{get_symbol_table()->intern("quote")}});
        quote_list.push_back(parse_interned(s));
        return SExpr{quote_list};
    }

    if (s[0] == '(') {
        return SExpr{parse_list_interned(s)};
    } else {
        return SExpr{parse_atom_interned(s)};
    }
}


// --- 3. Evaluator (AST -> Value) ---

constexpr SExpr eval(const SExpr& expr); // Forward declare

// Helper to extract a long from an evaluated SExpr
constexpr long get_long(const SExpr& e) {
    p_assert(e.atom.has_value(), "Expected a number");
    const auto& atom = *e.atom;
    p_assert(std::holds_alternative<long>(atom), "Expected a number");
    return std::get<long>(atom);
}

// =============================================================================
// WASM STRING COMPARISON WORKAROUND
// =============================================================================
// In WASM, string_view::operator== with string literals doesn't work
// reliably for interned symbols. This helper performs explicit char-by-char
// comparison which works correctly in both native and WASM builds.
// Marked constexpr so it works for compile-time evaluation too.
// Using __attribute__((noinline)) to prevent WASM optimizer issues.
// =============================================================================
#ifdef WASM_BUILD
__attribute__((noinline))
#endif
constexpr bool str_eq(std::string_view sv, const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    if (sv.size() != len) return false;
    for (size_t i = 0; i < len; i++) {
        if (sv[i] != s[i]) return false;
    }
    return true;
}

// apply_op() handles the built-in functions
// Operands are *already evaluated* SExprs
// (Using global str_eq function for WASM string comparison)
constexpr SExpr apply_op(std::string_view op, std::span<const SExpr> operands) {
    // C++20: std::transform_reduce is constexpr
    if (str_eq(op, "+")) {
        long result = std::transform_reduce(
            operands.begin(), operands.end(),
            0L, // Initial value
            std::plus<long>(), // Reduce operation
            [](const SExpr& e) { return get_long(e); } // Transform
        );
        return SExpr{Atom{result}}; // Return SExpr
    } else if (str_eq(op, "*")) {
        long result = std::transform_reduce(
            operands.begin(), operands.end(),
            1L, // Initial value
            std::multiplies<long>(), // Reduce operation
            [](const SExpr& e) { return get_long(e); } // Transform
        );
        return SExpr{Atom{result}}; // Return SExpr
    } else if (str_eq(op, "-")) {
        p_assert(!operands.empty(), "'-' requires at least one argument");
        long result = get_long(operands[0]);
        for (size_t i = 1; i < operands.size(); ++i) {
            result -= get_long(operands[i]);
        }
        return SExpr{Atom{result}}; // Return SExpr
    } else if (str_eq(op, "/")) {
        p_assert(operands.size() == 2, "'/' requires exactly two arguments");
        long val1 = get_long(operands[0]);
        long val2 = get_long(operands[1]);
        p_assert(val2 != 0, "Division by zero");
        return SExpr{Atom{val1 / val2}}; // Return SExpr
    }
    // === NEW LIST OPERATORS ===
    else if (str_eq(op, "car")) {
        p_assert(operands.size() == 1, "'car' requires one argument");
        const auto& arg = operands[0]; // Argument is already evaluated
        p_assert(arg.list.has_value(), "'car' argument must be a list");
        const auto& list = *arg.list;
        p_assert(!list.empty(), "'car' on empty list");
        return list.front(); // Return the first SExpr
    } else if (str_eq(op, "cdr")) {
        p_assert(operands.size() == 1, "'cdr' requires one argument");
        const auto& arg = operands[0]; // Argument is already evaluated
        p_assert(arg.list.has_value(), "'cdr' argument must be a list");
        const auto& list = *arg.list;
        p_assert(!list.empty(), "'cdr' on empty list");
        // Create a new list from the tail
        List new_list(list.begin() + 1, list.end());
        return SExpr{new_list}; // Return an SExpr containing the new list
    }
    else {
        p_assert(false, "Unknown operator");
    }
    return SExpr{Atom{0L}}; // Unreachable
}

// Main eval function (the "eval" from McCarthy's paper)
// Takes const& to avoid copies during recursion
constexpr SExpr eval(const SExpr& expr) {
    
    // Case 1: It's an Atom
    if (expr.atom.has_value()) {
        const auto& atom = *expr.atom;
        if (std::holds_alternative<long>(atom)) {
            return expr; // Numbers evaluate to themselves
        }
        if (std::holds_alternative<std::string_view>(atom)) {
            // This is where we would look up variables in an environment
            p_assert(false, "Unbound variable");
        }
    } 
    
    // Case 2: It's a List
    if (expr.list.has_value()) {
        const auto& list = *expr.list;
        p_assert(!list.empty(), "Cannot eval empty list");
        
        // Get operator (e.g., '+')
        const auto& op_expr = list[0];
        p_assert(op_expr.atom.has_value(), "Operator must be an atom");
        const auto& op_atom = *op_expr.atom;
        p_assert(std::holds_alternative<std::string_view>(op_atom), "Operator must be a symbol");
        auto op_str = std::get<std::string_view>(op_atom);

        // --- SPECIAL FORMS ---
        // 'quote' is a special form: it does NOT evaluate its arguments
        if (op_str == "quote") {
            p_assert(list.size() == 2, "'quote' requires exactly one argument");
            return list[1]; // Return the argument UNEVALUATED
        }

        // --- REGULAR FUNCTIONS ---
        // Evaluate all operands first
        List evaluated_operands;
        evaluated_operands.reserve(list.size() - 1);
        for(size_t i = 1; i < list.size(); ++i) {
            evaluated_operands.push_back(eval(list[i]));
        }
        
        // Apply the operator to the evaluated operands
        return apply_op(op_str, evaluated_operands);
    }

    p_assert(false, "Invalid SExpr"); // Should be unreachable
    return SExpr{Atom{0L}};
}

// --- Runtime Eval with Environment Support ---
// This version supports user-defined functions, defun, if, and comparisons
SExpr eval_with_env(const SExpr& expr, Env& env);

// Apply built-in ops OR user-defined functions
// (Using global str_eq function for WASM string comparison)
SExpr apply_with_env(std::string_view op, std::span<const SExpr> operands, Env& env) {
    // Comparison operators
    if (str_eq(op, "<")) {
        p_assert(operands.size() == 2, "'<' requires two arguments");
        return SExpr{Atom{get_long(operands[0]) < get_long(operands[1]) ? 1L : 0L}};
    }
    if (str_eq(op, ">")) {
        p_assert(operands.size() == 2, "'>' requires two arguments");
        return SExpr{Atom{get_long(operands[0]) > get_long(operands[1]) ? 1L : 0L}};
    }
    if (str_eq(op, "=")) {
        p_assert(operands.size() == 2, "'=' requires two arguments");
        return SExpr{Atom{get_long(operands[0]) == get_long(operands[1]) ? 1L : 0L}};
    }
    if (str_eq(op, "<=")) {
        p_assert(operands.size() == 2, "'<=' requires two arguments");
        return SExpr{Atom{get_long(operands[0]) <= get_long(operands[1]) ? 1L : 0L}};
    }
    if (str_eq(op, ">=")) {
        p_assert(operands.size() == 2, "'>=' requires two arguments");
        return SExpr{Atom{get_long(operands[0]) >= get_long(operands[1]) ? 1L : 0L}};
    }

    // Check if it's a user-defined function
    const Lambda* fn_ptr = env.lookup_fn(op);
    if (fn_ptr) {
        const auto& fn = *fn_ptr;
        p_assert(operands.size() == fn.params.size(), "Wrong number of arguments");

        // Create new environment with parameter bindings
        Env call_env = env;  // Copy current environment
        for (size_t i = 0; i < fn.params.size(); ++i) {
            call_env.define(fn.get_param(i), operands[i]);
        }

        // Evaluate body in new environment
        return eval_with_env(fn.get_body(), call_env);
    }

    // Fall back to built-in operators
    return apply_op(op, operands);
}

SExpr eval_with_env(const SExpr& expr, Env& env) {
    // Case 1: It's an Atom
    if (expr.atom.has_value()) {
        const auto& atom = *expr.atom;
        if (std::holds_alternative<long>(atom)) {
            return expr; // Numbers evaluate to themselves
        }
        if (std::holds_alternative<std::string_view>(atom)) {
            auto name = std::get<std::string_view>(atom);
            // Look up in environment
            const SExpr* val = env.lookup(name);
            if (val) {
                return *val;
            }
            p_assert(false, "Unbound variable");
        }
    }

    // Case 2: It's a List
    if (expr.list.has_value()) {
        const auto& list = *expr.list;
        p_assert(!list.empty(), "Cannot eval empty list");

        // Get operator
        const auto& op_expr = list[0];
        p_assert(op_expr.atom.has_value(), "Operator must be an atom");
        const auto& op_atom = *op_expr.atom;
        p_assert(std::holds_alternative<std::string_view>(op_atom), "Operator must be a symbol");
        auto op_str = std::get<std::string_view>(op_atom);

        // --- SPECIAL FORMS ---

        // 'quote' - return argument unevaluated
        if (str_eq(op_str, "quote")) {
            p_assert(list.size() == 2, "'quote' requires exactly one argument");
            return list[1];
        }

        // 'if' - conditional evaluation
        if (str_eq(op_str, "if")) {
            p_assert(list.size() == 4, "'if' requires exactly 3 arguments: (if cond then else)");
            auto cond = eval_with_env(list[1], env);
            long cond_val = get_long(cond);
            return cond_val != 0
                ? eval_with_env(list[2], env)
                : eval_with_env(list[3], env);
        }

        // 'defun' - define a named function
        if (str_eq(op_str, "defun")) {
            p_assert(list.size() == 4, "'defun' requires: (defun name (params...) body)");

            // Get function name
            const auto& name_expr = list[1];
            p_assert(name_expr.atom.has_value(), "Function name must be a symbol");
            p_assert(std::holds_alternative<std::string_view>(*name_expr.atom),
                     "Function name must be a symbol");
            auto name = std::get<std::string_view>(*name_expr.atom);

            // Get parameters
            const auto& params_expr = list[2];
            p_assert(params_expr.list.has_value(), "Parameters must be a list");
            std::vector<std::string_view> params;
            for (const auto& p : *params_expr.list) {
                p_assert(p.atom.has_value(), "Parameter must be a symbol");
                p_assert(std::holds_alternative<std::string_view>(*p.atom),
                         "Parameter must be a symbol");
                params.push_back(std::get<std::string_view>(*p.atom));
            }

            // Store the function in environment
            Lambda fn(std::move(params), list[3]);
            env.define_fn(name, std::move(fn));

            // Return the function name as confirmation
            return SExpr{Atom{name}};
        }

        // --- REGULAR FUNCTION APPLICATION ---
        // Evaluate all operands first
        List evaluated_operands;
        evaluated_operands.reserve(list.size() - 1);
        for (size_t i = 1; i < list.size(); ++i) {
            evaluated_operands.push_back(eval_with_env(list[i], env));
        }

        // Apply the operator
        return apply_with_env(op_str, evaluated_operands, env);
    }

    p_assert(false, "Invalid SExpr");
    return SExpr{Atom{0L}};
}

} // namespace MiniLisp
// --- End of Core Lisp Interpreter ---


// --- COMPILE-TIME Entry Point ---
// This UDL remains `consteval` because it *must* run at compile-time.
// It now asserts that the *final* result must be a number.
template <FixedString S>
consteval auto operator""_lisp() {
    std::string_view s = S.get();
    
    // 1. Parse the string into an AST (calls constexpr parse)
    auto ast = MiniLisp::parse(s);

    // 2. Evaluate the AST (calls constexpr eval)
    auto result_sexpr = MiniLisp::eval(ast);

    // 3. Extract the final 'long' value for static_assert
    // Our interpreter's "top level" must return a number.
    MiniLisp::p_assert(result_sexpr.atom.has_value(), "Final result must be an atom");
    const auto& atom = *result_sexpr.atom;
    MiniLisp::p_assert(std::holds_alternative<long>(atom), "Final result must be a number");
    return std::get<long>(atom);
}

#ifdef MINIMAL_BUILD
// --- POSIX I/O Helpers for Minimal Build ---
void write_str(const char* str) {
    write(STDOUT_FILENO, str, strlen(str));
}

void write_number(long num) {
    char buffer[32];
    int i = 0;
    bool is_neg = num < 0;
    if (is_neg) num = -num;

    if (num == 0) {
        buffer[i++] = '0';
    } else {
        char temp[32];
        int j = 0;
        while (num > 0) {
            temp[j++] = '0' + (num % 10);
            num /= 10;
        }
        if (is_neg) buffer[i++] = '-';
        while (j > 0) {
            buffer[i++] = temp[--j];
        }
    }
    buffer[i] = '\0';
    write_str(buffer);
}

int read_line_posix(char* buffer, int max_len) {
    int i = 0;
    char c;
    while (i < max_len - 1) {
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0 || c == '\n') break;
        buffer[i++] = c;
    }
    buffer[i] = '\0';
    return i;
}
#endif

// --- RUNTIME Entry Point ---
// This is a new function that can be called at runtime
// with a normal std::string or std::string_view.
long eval_lisp_runtime(std::string_view s) {
    // The MiniLisp::parse and ::eval functions are marked
    // 'constexpr', so they are available at runtime too.
#if !defined(MINIMAL_BUILD) && !defined(WASM_BUILD)
    try {
        auto ast = MiniLisp::parse(s);
        auto result_sexpr = MiniLisp::eval(ast);

        // Extract final 'long' value for the REPL
        MiniLisp::p_assert(result_sexpr.atom.has_value(), "Final result must be an atom");
        const auto& atom = *result_sexpr.atom;
        MiniLisp::p_assert(std::holds_alternative<long>(atom), "Final result must be a number");
        return std::get<long>(atom);

    } catch (const std::exception& e) {
        // Re-throw or handle runtime errors
        std::cerr << "Runtime Lisp Error: " << e.what() << std::endl;
        return 0; // Or some error code
    }
#else
    // Minimal/WASM build: no exception handling
    auto ast = MiniLisp::parse(s);
    auto result_sexpr = MiniLisp::eval(ast);

    MiniLisp::p_assert(result_sexpr.atom.has_value(), "Final result must be an atom");
    const auto& atom = *result_sexpr.atom;
    MiniLisp::p_assert(std::holds_alternative<long>(atom), "Final result must be a number");
    return std::get<long>(atom);
#endif
}


#ifndef WASM_BUILD
// 4. Main function to prove it works
int main() {
    // --- COMPILE-TIME Evaluation ---
    // This all happens at compile-time!
    constexpr auto val = "(+ 10 (* 2 5))"_lisp;
    constexpr auto val2 = "(- 100 (* 2 (+ 10 20 5)))"_lisp;

    // If these lines compile, it worked.
    static_assert(val == 20);
    static_assert(val2 == 30); // 100 - (2 * (10 + 20 5)) = 100 - (2 * 35) = 100 - 70 = 30

    // === NEW COMPILE-TIME TESTS for car/cdr/quote ===
    // Use ' (quote) syntax
    constexpr auto val3 = "(car '(10 20 30))"_lisp;
    static_assert(val3 == 10);

    // Use full (quote ...) syntax
    constexpr auto val4 = "(car (cdr (quote (10 20 30))))"_lisp;
    static_assert(val4 == 20);

    // Combine with arithmetic
    constexpr auto val5 = "(+ (car '(10 5)) (car (cdr '(3 20))))"_lisp;
    static_assert(val5 == 30); // 10 + 20

#ifndef MINIMAL_BUILD
    std::cout << "Compile-time tests passed!" << std::endl;

    // --- RUNTIME Evaluation (REPL) with Environment ---
    std::cout << "\n--- MiniLisp Runtime REPL ---" << std::endl;
    std::cout << "Supports: defun, if, <, >, =, <=, >=" << std::endl;
    std::cout << "Enter Lisp expression or 'q' to quit." << std::endl;

    MiniLisp::FunctionStore repl_fn_store;
    MiniLisp::Env repl_env(&repl_fn_store);  // Persistent environment for REPL
    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line) || line == "q") {
            break;
        }

        if (line.empty()) continue;

        try {
            std::string_view sv(line);
            // Use interning parser for runtime - ensures symbol lifetime
            auto ast = MiniLisp::parse_interned(sv);
            auto result = MiniLisp::eval_with_env(ast, repl_env);

            // Print result
            if (result.atom.has_value()) {
                const auto& atom = *result.atom;
                if (std::holds_alternative<long>(atom)) {
                    std::cout << "=> " << std::get<long>(atom) << std::endl;
                } else {
                    std::cout << "=> " << std::get<std::string_view>(atom) << std::endl;
                }
            } else {
                std::cout << "=> (list)" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
#else
    // MINIMAL_BUILD: Use POSIX I/O
    write_str("Compile-time tests passed!\n");
    write_str("\n--- MiniLisp Runtime REPL ---\n");
    write_str("Enter Lisp expression (e.g., \"(car '(1 2))\") or 'q' to quit.\n");

    char line[512];
    while (true) {
        write_str("> ");
        int len = read_line_posix(line, sizeof(line));

        if (len == 0 || (len == 1 && line[0] == 'q')) {
            break;
        }

        if (len == 0) continue;

        // Call the runtime-capable function
        long result = eval_lisp_runtime(std::string_view(line, len));
        write_str("=> ");
        write_number(result);
        write_str("\n");
    }
#endif

    return 0;
}
#endif // WASM_BUILD
