// wasm.cpp - WASM entry point for MiniLisp
// ============================================================================
// BUFFER OVERLAP BUG (Fixed)
// ============================================================================
// The WASM data section stores string literals used for operator comparison
// (e.g., "defun", "if", "quote"). These literals can be as low as ~1068 bytes
// from the start of memory.
//
// When JavaScript writes input strings to memory, it MUST use an offset that
// doesn't overlap with the data section. Using offset 1024 caused corruption:
// - Input: "(defun factorial (n) ...)" written at offset 1024
// - String literal "defun" located at offset 1068
// - The input overwrote the literal with "torial" (part of "factorial")
// - str_eq("defun", "defun") then failed because the literal was corrupted
//
// Solution: Use get_buffer_offset() which returns a safe offset (65536 = 64KB)
// that's well beyond the data section.
// ============================================================================
#define WASM_BUILD
#include "main.cpp"
#include <cstdlib>

// Lazy initialization to avoid WASM static init order issues
static MiniLisp::FunctionStore* get_fn_store() {
    static MiniLisp::FunctionStore store;
    return &store;
}

static MiniLisp::Env* get_global_env() {
    static MiniLisp::Env env(get_fn_store());
    return &env;
}

// Safe buffer offset - well beyond WASM data section
// The data section typically ends around 4-8KB, using 64KB to be safe
static constexpr long SAFE_BUFFER_OFFSET = 65536;

// Track last input size for JS to query
static long g_last_input_len = 0;

extern "C" {

// Return safe buffer offset for JavaScript to use
// JavaScript should write input strings to this offset
__attribute__((export_name("get_buffer_offset")))
long get_buffer_offset() {
    return SAFE_BUFFER_OFFSET;
}

// Count defined functions (useful for testing)
__attribute__((export_name("fn_count")))
long fn_count() {
    return static_cast<long>(get_fn_store()->functions.size());
}

// Count interned symbols (useful for testing)
__attribute__((export_name("sym_count")))
long sym_count() {
    return static_cast<long>(MiniLisp::get_symbol_table()->size());
}

// Get last input length
__attribute__((export_name("last_input_len")))
long last_input_len() {
    return g_last_input_len;
}

// Evaluate Lisp expression with persistent environment
// Returns the numeric result, or 0 for non-numeric results (like defun)
// Uses parse_interned to ensure all symbols are stored in the global SymbolTable,
// so string_views remain valid even when the input buffer is reused.
__attribute__((export_name("eval")))
long eval_lisp(const char* input) {
    std::string_view sv(input);
    g_last_input_len = static_cast<long>(sv.size());
    auto ast = MiniLisp::parse_interned(sv);
    auto result = MiniLisp::eval_with_env(ast, *get_global_env());

    // Return long for numeric results
    if (result.atom.has_value()) {
        if (std::holds_alternative<long>(*result.atom)) {
            return std::get<long>(*result.atom);
        }
    }
    // Non-numeric result (e.g., defun returns function name)
    return 0;
}

// Reset the global environment (clear all function definitions)
__attribute__((export_name("reset_env")))
void reset_env() {
    get_global_env()->clear();
}

} // extern "C"

// C++ operator overloads - wasi-sdk provides malloc/free
void* operator new(size_t size) { return malloc(size); }
void* operator new[](size_t size) { return malloc(size); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void*, size_t) noexcept {}
void operator delete[](void*, size_t) noexcept {}
