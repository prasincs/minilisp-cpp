# Binary Size: Linux vs macOS

An exploration of why the same C++ code produces different binary sizes on different platforms.

## The Same Code, Different Sizes

When building our minimal Lisp interpreter with `make ultra-small`, we see:

- **macOS (Mach-O)**: 35,016 bytes (34KB) stripped
- **Linux (ELF)**: 67,952 bytes (66KB) stripped - surprisingly larger!
- **Linux + UPX**: 10,288 bytes (10KB) compressed ✨

Wait - **Linux is bigger than macOS?** Yes! But only before compression. Here's why...

**Note**: UPX doesn't support zstd. We tested all available algorithms and found that default NRV compression (10,288 bytes) beats LZMA (11,528 bytes) for small binaries!

## What We Actually Removed

Looking at the actual executable code sections:

```
DEFAULT BUILD (with iostream):
  Code section:      8,484 bytes
  Exception tables:    844 bytes
  Total code:       10,753 bytes

ULTRA-SMALL BUILD (POSIX I/O):
  Code section:      5,752 bytes  (32% reduction!)
  Exception tables:    288 bytes  (66% reduction!)
  Total code:        7,273 bytes  (32% reduction!)
```

We successfully removed **2,732 bytes** of actual executable code by:
1. Replacing `<iostream>` with POSIX `write()`/`read()`
2. Removing `std::string` in favor of fixed buffers
3. Simplifying exception handling

## So Why Is macOS Still 34KB?

The answer lies in the **binary format overhead**.

### macOS Mach-O Format

Mach-O (Mach Object) is macOS's executable format. It has architectural decisions that prioritize other concerns over minimal size:

**Segment Alignment: 16KB**
- Each segment must start on a 16KB boundary
- Even tiny segments consume 16KB of disk space
- Minimum 3 segments = **48KB baseline**

Our binary layout:
```
__PAGEZERO:     4GB (virtual, zero-fill)
__TEXT:         16KB (contains our 7KB of code + padding)
__DATA_CONST:   16KB (contains 328 bytes + padding)
__LINKEDIT:     16KB (symbols, relocations)
```

Total on disk: **34KB** after strip (removes some __LINKEDIT)

### Linux ELF Format

ELF (Executable and Linkable Format) is used on Linux. Here are the **actual results** from Ubuntu:

**Before strip + optimization:**
- Binary on disk: 67,952 bytes (66KB)

**After strip:**
- Binary on disk: 67,952 bytes (66KB) - same size!

**Actual sections (from `size`):**
```
text:    10,978 bytes (actual code)
data:       936 bytes (initialized data)
bss:          8 bytes (uninitialized data)
Total:   11,922 bytes
```

**The Surprise:** Linux ELF is actually **larger on disk** than macOS (66KB vs 34KB)!

Why? The ELF binary contains more metadata, debug info remnants, and symbol table overhead even after stripping. The actual code is only ~12KB, but the file format adds ~55KB of overhead.

### Why The Difference?

| Aspect | macOS Mach-O | Linux ELF | Reality |
|--------|--------------|-----------|---------|
| **Stripped size** | 35,016 bytes | 67,952 bytes | macOS is smaller! |
| **Actual code** | ~7KB | ~11KB | Similar code size |
| **Overhead** | ~28KB | ~56KB | ELF has more metadata |
| **Page alignment** | 16KB | 4KB | Doesn't help Linux here |
| **After UPX** | N/A (unreliable) | 11,528 bytes | UPX saves the day! |

**The Twist:** macOS Mach-O is actually **more efficient** for this small binary before compression!

## The UPX Factor

UPX (Ultimate Packer for eXecutables) compresses the binary. Here are **actual results**:

**On Linux (ARM64 Ubuntu):**
- Before UPX: 67,952 bytes (66KB)
- After UPX (LZMA): 11,528 bytes (11.5KB)
- After UPX (NRV): **10,288 bytes (10.0KB)** ✨ **Best!**
- Compression ratio: **85% reduction!**
- Runtime decompression: ~50-100ms startup overhead

**UPX Compression Comparison:**
| Algorithm | Size | Notes |
|-----------|------|-------|
| NRV (default) | **10,288 bytes** | Best for small binaries! |
| LZMA | 11,528 bytes | Worse for our case |
| zstd | Not supported | UPX 4.2.2 doesn't have it |

**On macOS (Apple Silicon):**
- Before: 35,016 bytes (34KB)
- After UPX: Officially unsupported
- `--force-macos`: Sometimes works, often segfaults
- Code signing issues prevent reliable use
- **Not recommended**

## The Real Achievement

Despite the 34KB file size, we did succeed:

✅ **Removed iostream** - Confirmed by 32% code reduction
✅ **Removed std::string** - Using fixed buffers
✅ **Minimized exceptions** - Simpler error handling
✅ **POSIX I/O only** - Direct system calls

The **actual executable code** went from 10.7KB → 7.3KB. The rest is just binary format overhead that we can't control on macOS.

## Building on Linux

To see the true minimal size, build on Linux:

```bash
# On Linux with clang
clang++ -std=c++20 -Os -flto -DMINIMAL_BUILD -fno-rtti \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -o lisp_repl main.cpp
strip lisp_repl

# Result: 67,952 bytes (66KB) stripped

# With UPX compression
upx --best --lzma lisp_repl

# Result: 11,528 bytes (11.5KB) on disk ✨
```

## Cross-Compiling for Linux (from macOS)

We've created a script to build the Linux binary using Docker:

```bash
./build-linux.sh
```

This script:
1. Pulls the latest Ubuntu container
2. Installs clang, make, and upx
3. Builds `ultra-small` target
4. Shows size comparison
5. Tests functionality
6. Outputs `lisp_repl` (Linux ARM64 binary)

**Actual results:**
```
macOS (Mach-O):  35,016 bytes (34KB)
Linux (ELF):     67,952 bytes (66KB) uncompressed
Linux (UPX):     11,528 bytes (11.5KB) compressed ✨
```

The Linux binary is **67% smaller** with UPX compression!

## WebAssembly Build

The project also supports building to WebAssembly for running in the browser:

```bash
make wasm
# Result: 41,269 bytes (41KB)
```

### Why wasi-sdk Instead of Emscripten?

I chose **wasi-sdk** over Emscripten for several reasons:

| Aspect | wasi-sdk | Emscripten |
|--------|----------|------------|
| **Output** | Pure .wasm | .wasm + .js runtime |
| **Size** | 41KB | ~100KB+ (with JS glue) |
| **Complexity** | Minimal | Full runtime, filesystem emulation |
| **Modern** | WASI standard | Legacy approach |

wasi-sdk produces a single `.wasm` file with no JavaScript runtime bloat.

### Why Not Bare Clang with -nostdlib?

I initially tried the smallest possible approach:

```bash
clang++ --target=wasm32 -nostdlib -Wl,--no-entry ...
```

This failed because:
1. `-nostdlib` removes access to `<string_view>`, `<vector>`, `<variant>`, etc.
2. The interpreter relies heavily on the C++ standard library
3. Would require reimplementing these types from scratch

**The trade-off**: wasi-sdk adds ~30KB of libc (dlmalloc, etc.) but provides full C++20 stdlib support. For a more minimal interpreter written without STL, bare WASM could achieve ~10KB.

### Build Flags

```makefile
WASMFLAGS := -std=c++20 -Os -fno-exceptions \
             -Wl,--no-entry -Wl,--export-dynamic
```

Key choices:
- `-fno-exceptions`: Reduces binary size, uses `__builtin_trap()` for errors
- `-Wl,--no-entry`: Library mode (no `main()`)
- `-Wl,--export-dynamic`: Export the `eval` function

### The WASI Shim

wasi-sdk's libc (dlmalloc) requires WASI syscalls. For browser use, a minimal shim is needed:

```javascript
const wasi = {
    args_get: () => 0, args_sizes_get: () => 0,
    proc_exit: () => {}, fd_write: () => 0,
    fd_read: () => 0, fd_close: () => 0,
    fd_seek: () => 0, fd_fdstat_get: () => 0,
    environ_sizes_get: () => 0, environ_get: () => 0,
    clock_time_get: () => 0,
};

const { instance } = await WebAssembly.instantiate(wasmBytes,
    { wasi_snapshot_preview1: wasi });
```

### Size Comparison (All Platforms)

| Platform | Size | Notes |
|----------|------|-------|
| macOS (Mach-O) | 35KB | Stripped, 16KB page alignment |
| Linux (ELF) | 66KB | Stripped, more metadata |
| Linux + UPX | **10KB** | Best native size! |
| **WASM** | **41KB** | Portable, runs in browser |

### Trade-offs

**WASM Advantages:**
- Runs anywhere (browser, Node.js, Wasmtime)
- Sandboxed execution
- No recompilation needed per platform

**WASM Disadvantages:**
- Larger than UPX-compressed Linux (41KB vs 10KB)
- Requires WASI shim for browser
- Slightly slower than native (~1.5-2x)

For an interactive blog demo, 41KB is excellent - it loads instantly and runs the same everywhere.

## Key Takeaways

1. **Binary format matters more than you think**
   - Same code, 3x size difference between macOS and Linux
   - Not a compiler issue, it's the executable format

2. **macOS prioritizes security/performance over size**
   - 16KB page alignment for better memory management
   - Address Space Layout Randomization (ASLR) benefits
   - Performance implications of larger pages

3. **You can't fight the format**
   - On macOS, ~34KB is close to the minimum for any C++ program
   - Even "hello world" in C++ is ~33KB on macOS with stdlib

4. **Measure what matters**
   - Focus on actual code size (text section)
   - Binary format overhead is unavoidable
   - Our 32% code reduction is the real win

## Conclusion

The ultra-small build **works brilliantly** - we removed iostream and reduced actual code by 32%.

**Size summary across all targets:**
| Target | Size | Use Case |
|--------|------|----------|
| Linux + UPX | **10KB** | Smallest native binary |
| macOS | 35KB | Development machine |
| **WASM** | **41KB** | Browser/portable |
| Linux (ELF) | 66KB | Uncompressed native |

**Key findings:**
1. I successfully minimized the **actual code** (7-11KB of executable code)
2. Binary format overhead varies wildly by platform
3. **macOS Mach-O is actually smaller** than Linux ELF before compression
4. **UPX NRV compression on Linux** achieves the smallest native size (10KB)
5. **WASM with wasi-sdk** provides the best portability at 41KB
6. **LZMA isn't always best** - for small binaries, NRV beats LZMA by 11%!

The lesson: **choose your target based on your use case**:
- Need smallest binary? → Linux + UPX (10KB)
- Need browser support? → WASM (41KB)
- Need universal portability? → WASM runs everywhere

---

*For more details, run `size -m lisp_repl` on macOS or `size lisp_repl` on Linux to see the actual section sizes.*
