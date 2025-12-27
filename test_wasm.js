// test_wasm.js - Automated WASM test suite for MiniLisp
// =============================================================================
// Run with: node test_wasm.js
//
// Tests the WASM build of the Lisp interpreter, focusing on:
// 1. Basic arithmetic operations
// 2. Comparison operators
// 3. Conditional expressions (if)
// 4. Simple function definitions (defun)
// 5. Recursive function definitions (the bug we're fixing!)
// 6. Multiple function definitions
//
// The key test is recursive functions - these previously failed because
// string_view pointers in the Lambda body became invalid when the WASM
// input buffer was reused. With symbol interning, all symbols live in
// permanent storage, fixing this issue.
// =============================================================================

const fs = require('fs');
const { WASI } = require('wasi');
const assert = require('assert');

async function runTests() {
    // Initialize WASI and load WASM module
    const wasi = new WASI({ version: 'preview1' });
    const wasmPath = './lisp.wasm';

    if (!fs.existsSync(wasmPath)) {
        console.error('Error: lisp.wasm not found. Run "make wasm" first.');
        process.exit(1);
    }

    const wasm = fs.readFileSync(wasmPath);
    const module = await WebAssembly.compile(wasm);
    const instance = await WebAssembly.instantiate(module, {
        wasi_snapshot_preview1: wasi.wasiImport
    });

    const { memory, eval: evalFn, fn_count, reset_env, get_buffer_offset } = instance.exports;

    // Helper to evaluate Lisp code
    // IMPORTANT: Use get_buffer_offset() to get a safe offset that doesn't
    // overlap with the WASM data section (which contains string literals)
    const INPUT_BUFFER_OFFSET = get_buffer_offset();
    function evalLisp(code) {
        const bytes = new TextEncoder().encode(code + '\0');
        new Uint8Array(memory.buffer, INPUT_BUFFER_OFFSET, bytes.length).set(bytes);
        return evalFn(INPUT_BUFFER_OFFSET);
    }

    // Test runner with colored output
    let passed = 0;
    let failed = 0;

    function test(name, fn) {
        try {
            fn();
            console.log(`\x1b[32m  PASS\x1b[0m ${name}`);
            passed++;
        } catch (e) {
            console.log(`\x1b[31m  FAIL\x1b[0m ${name}`);
            console.log(`       Expected: ${e.expected}, Actual: ${e.actual || e.message}`);
            failed++;
        }
    }

    function assertEqual(actual, expected, message) {
        if (actual !== expected) {
            const err = new Error(message || `${actual} !== ${expected}`);
            err.expected = expected;
            err.actual = actual;
            throw err;
        }
    }

    console.log('\n=== MiniLisp WASM Test Suite ===\n');

    // --- Basic Arithmetic ---
    console.log('Basic Arithmetic:');
    test('addition: (+ 1 2 3) = 6', () => {
        assertEqual(evalLisp('(+ 1 2 3)'), 6);
    });
    test('multiplication: (* 6 7) = 42', () => {
        assertEqual(evalLisp('(* 6 7)'), 42);
    });
    test('subtraction: (- 10 3) = 7', () => {
        assertEqual(evalLisp('(- 10 3)'), 7);
    });
    test('division: (/ 42 6) = 7', () => {
        assertEqual(evalLisp('(/ 42 6)'), 7);
    });
    test('nested: (+ (* 3 4) (- 10 5)) = 17', () => {
        assertEqual(evalLisp('(+ (* 3 4) (- 10 5))'), 17);
    });

    // --- Comparison Operators ---
    console.log('\nComparison Operators:');
    test('less than (true): (< 1 2) = 1', () => {
        assertEqual(evalLisp('(< 1 2)'), 1);
    });
    test('less than (false): (< 2 1) = 0', () => {
        assertEqual(evalLisp('(< 2 1)'), 0);
    });
    test('greater than (true): (> 5 3) = 1', () => {
        assertEqual(evalLisp('(> 5 3)'), 1);
    });
    test('equal (true): (= 42 42) = 1', () => {
        assertEqual(evalLisp('(= 42 42)'), 1);
    });
    test('equal (false): (= 1 2) = 0', () => {
        assertEqual(evalLisp('(= 1 2)'), 0);
    });

    // --- Conditionals ---
    console.log('\nConditionals:');
    test('if true branch: (if 1 42 0) = 42', () => {
        assertEqual(evalLisp('(if 1 42 0)'), 42);
    });
    test('if false branch: (if 0 42 99) = 99', () => {
        assertEqual(evalLisp('(if 0 42 99)'), 99);
    });
    test('if with comparison: (if (< 1 2) 100 200) = 100', () => {
        assertEqual(evalLisp('(if (< 1 2) 100 200)'), 100);
    });

    // --- Simple Function Definition ---
    console.log('\nSimple Function Definition:');
    reset_env();
    test('defun square', () => {
        evalLisp('(defun square (x) (* x x))');
        assertEqual(fn_count(), 1);
    });
    test('call square(7) = 49', () => {
        assertEqual(evalLisp('(square 7)'), 49);
    });
    test('call square(12) = 144', () => {
        assertEqual(evalLisp('(square 12)'), 144);
    });

    // --- THE KEY TEST: Recursive Functions ---
    // This is the bug we're fixing! Previously failed with "unreachable"
    console.log('\nRecursive Functions (THE BUG FIX):');
    reset_env();
    test('defun factorial (recursive!)', () => {
        evalLisp('(defun factorial (n) (if (< n 2) 1 (* n (factorial (- n 1)))))');
        assertEqual(fn_count(), 1);
    });
    test('factorial(0) = 1', () => {
        assertEqual(evalLisp('(factorial 0)'), 1);
    });
    test('factorial(1) = 1', () => {
        assertEqual(evalLisp('(factorial 1)'), 1);
    });
    test('factorial(5) = 120', () => {
        assertEqual(evalLisp('(factorial 5)'), 120);
    });
    test('factorial(6) = 720', () => {
        assertEqual(evalLisp('(factorial 6)'), 720);
    });
    test('factorial(10) = 3628800', () => {
        assertEqual(evalLisp('(factorial 10)'), 3628800);
    });

    // --- Another Recursive Function: Fibonacci ---
    console.log('\nFibonacci (another recursive test):');
    reset_env();
    test('defun fib (recursive)', () => {
        evalLisp('(defun fib (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))');
        assertEqual(fn_count(), 1);
    });
    test('fib(0) = 0', () => {
        assertEqual(evalLisp('(fib 0)'), 0);
    });
    test('fib(1) = 1', () => {
        assertEqual(evalLisp('(fib 1)'), 1);
    });
    test('fib(10) = 55', () => {
        assertEqual(evalLisp('(fib 10)'), 55);
    });

    // --- Multiple Functions ---
    console.log('\nMultiple Functions:');
    reset_env();
    test('define double and triple', () => {
        evalLisp('(defun double (x) (* x 2))');
        evalLisp('(defun triple (x) (* x 3))');
        assertEqual(fn_count(), 2);
    });
    test('double(5) = 10', () => {
        assertEqual(evalLisp('(double 5)'), 10);
    });
    test('triple(5) = 15', () => {
        assertEqual(evalLisp('(triple 5)'), 15);
    });
    test('compose: (double (triple 4)) = 24', () => {
        assertEqual(evalLisp('(double (triple 4))'), 24);
    });

    // --- Function Redefinition ---
    console.log('\nFunction Redefinition:');
    reset_env();
    test('define f as (* x 2)', () => {
        evalLisp('(defun f (x) (* x 2))');
        assertEqual(evalLisp('(f 5)'), 10);
    });
    test('redefine f as (* x 3)', () => {
        evalLisp('(defun f (x) (* x 3))');
        assertEqual(evalLisp('(f 5)'), 15);
    });
    test('fn_count still 1 after redefine', () => {
        assertEqual(fn_count(), 1);
    });

    // --- Summary ---
    console.log('\n=== Test Results ===');
    console.log(`\x1b[32m${passed} passed\x1b[0m, \x1b[31m${failed} failed\x1b[0m`);

    if (failed > 0) {
        console.log('\nSome tests failed!');
        process.exit(1);
    } else {
        console.log('\nAll tests passed!');
        process.exit(0);
    }
}

runTests().catch(e => {
    console.error('Test suite error:', e);
    process.exit(1);
});
