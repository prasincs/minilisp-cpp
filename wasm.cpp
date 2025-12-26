// wasm.cpp - WASM entry point for MiniLisp
#define WASM_BUILD
#include "main.cpp"
#include <cstdlib>

extern "C" {

// Exported WASM functions
__attribute__((export_name("eval")))
long eval_lisp(const char* input) {
    return eval_lisp_runtime(std::string_view(input));
}

} // extern "C"

// C++ operator overloads - wasi-sdk provides malloc/free
void* operator new(size_t size) { return malloc(size); }
void* operator new[](size_t size) { return malloc(size); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void*, size_t) noexcept {}
void operator delete[](void*, size_t) noexcept {}
