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

// --- 1. AST (Abstract Syntax Tree) Data Structures ---

struct SExpr; // Forward declaration

// An "Atom" is either a number (long) or a symbol (string_view)
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


// --- 2. Parser (String -> AST) ---

// Helper to throw errors (can be used at compile-time or runtime)
constexpr void p_assert(bool cond, const char* msg) {
    if (!cond) {
        // At compile-time, this fails compilation.
        // At runtime, this throws an exception (or traps for WASM).
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


// --- 3. Evaluator (AST -> Value) ---

constexpr SExpr eval(const SExpr& expr); // Forward declare

// Helper to extract a long from an evaluated SExpr
constexpr long get_long(const SExpr& e) {
    p_assert(e.atom.has_value(), "Expected a number");
    const auto& atom = *e.atom;
    p_assert(std::holds_alternative<long>(atom), "Expected a number");
    return std::get<long>(atom);
}

// apply_op() handles the built-in functions
// Operands are *already evaluated* SExprs
constexpr SExpr apply_op(std::string_view op, std::span<const SExpr> operands) {
    
    // C++20: std::transform_reduce is constexpr
    if (op == "+") {
        long result = std::transform_reduce(
            operands.begin(), operands.end(), 
            0L, // Initial value
            std::plus<long>(), // Reduce operation
            [](const SExpr& e) { return get_long(e); } // Transform
        );
        return SExpr{Atom{result}}; // Return SExpr
    } else if (op == "*") {
        long result = std::transform_reduce(
            operands.begin(), operands.end(),
            1L, // Initial value
            std::multiplies<long>(), // Reduce operation
            [](const SExpr& e) { return get_long(e); } // Transform
        );
        return SExpr{Atom{result}}; // Return SExpr
    } else if (op == "-") {
        p_assert(!operands.empty(), "'-' requires at least one argument");
        long result = get_long(operands[0]);
        for (size_t i = 1; i < operands.size(); ++i) {
            result -= get_long(operands[i]);
        }
        return SExpr{Atom{result}}; // Return SExpr
    } else if (op == "/") {
        p_assert(operands.size() == 2, "'/' requires exactly two arguments");
        long val1 = get_long(operands[0]);
        long val2 = get_long(operands[1]);
        p_assert(val2 != 0, "Division by zero");
        return SExpr{Atom{val1 / val2}}; // Return SExpr
    } 
    // === NEW LIST OPERATORS ===
    else if (op == "car") {
        p_assert(operands.size() == 1, "'car' requires one argument");
        const auto& arg = operands[0]; // Argument is already evaluated
        p_assert(arg.list.has_value(), "'car' argument must be a list");
        const auto& list = *arg.list;
        p_assert(!list.empty(), "'car' on empty list");
        return list.front(); // Return the first SExpr
    } else if (op == "cdr") {
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

    // --- RUNTIME Evaluation (REPL) ---
    std::cout << "\n--- MiniLisp Runtime REPL ---" << std::endl;
    std::cout << "Enter Lisp expression (e.g., \"(car '(1 2))\") or 'q' to quit." << std::endl;

    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line) || line == "q") {
            break;
        }

        if (line.empty()) continue;

        try {
            // Call the runtime-capable function
            long result = eval_lisp_runtime(line);
            std::cout << "=> " << result << std::endl;
        } catch (const std::exception& e) {
            // This catch is mostly for safety; eval_lisp_runtime also has one
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
