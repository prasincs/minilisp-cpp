# MiniLisp C++

A **compile-time AND runtime Lisp interpreter** written in C++20.

This project implements a minimal Lisp interpreter (similar to McCarthy's original Lisp) that can parse and evaluate Lisp expressions both at compile-time (using C++20 `constexpr`) and at runtime. It showcases modern C++20 features like `std::span`, `std::variant`, and `std::transform_reduce`.

## Features

- **Compile-time evaluation**: Lisp expressions are evaluated during compilation using C++20 `constexpr` and `consteval`
- **Runtime REPL**: Interactive Read-Eval-Print Loop for runtime evaluation
- **Basic arithmetic**: `+`, `-`, `*`, `/` operators
- **List operations**: `car`, `cdr`, `quote` (with `'` syntax sugar)
- **Modern C++20**: Uses constexpr containers, std::variant, std::span, and more

## Building

### Using Make (with clang++)

```bash
make
```

To build and run:

```bash
make run
```

To clean build artifacts:

```bash
make clean
```

### Manual Build

Using clang++:

```bash
clang++ -std=c++20 -Wall -Wextra -O2 -o lisp_repl main.cpp
```

Using g++:

```bash
g++ -std=c++20 -Wall -Wextra -O2 -o lisp_repl main.cpp
```

## Usage

### Compile-time Examples

The interpreter evaluates expressions at compile-time and validates them with `static_assert`:

```cpp
// Arithmetic
constexpr auto val = "(+ 10 (* 2 5))"_lisp;
static_assert(val == 20);

constexpr auto val2 = "(- 100 (* 2 (+ 10 20 5)))"_lisp;
static_assert(val2 == 30);

// List operations with quote
constexpr auto val3 = "(car '(10 20 30))"_lisp;
static_assert(val3 == 10);

constexpr auto val4 = "(car (cdr (quote (10 20 30))))"_lisp;
static_assert(val4 == 20);

// Combined operations
constexpr auto val5 = "(+ (car '(10 5)) (car (cdr '(3 20))))"_lisp;
static_assert(val5 == 30);
```

### Runtime REPL

Run the executable to start an interactive REPL:

```bash
./lisp_repl
```

Example session:

```
--- MiniLisp Runtime REPL ---
Enter Lisp expression (e.g., "(car '(1 2))") or 'q' to quit.
> (+ 1 2 3)
=> 6
> (* 5 (- 10 3))
=> 35
> (car '(42 100 200))
=> 42
> (car (cdr '(10 20 30)))
=> 20
> q
```

## Supported Operations

### Arithmetic Operators

- `(+ a b c ...)` - Addition
- `(- a b c ...)` - Subtraction
- `(* a b c ...)` - Multiplication
- `(/ a b)` - Division (requires exactly 2 arguments)

### List Operations

- `(quote expr)` or `'expr` - Returns expression unevaluated
- `(car list)` - Returns first element of list
- `(cdr list)` - Returns tail of list (all elements except first)

## Extending the Interpreter

### Adding New Functions

You can easily add new built-in functions by modifying the `apply_op` function in `main.cpp` around line 203. New functions will automatically work in both compile-time and runtime contexts.

#### Step-by-Step Guide

1. **Locate the `apply_op` function** in the `MiniLisp` namespace (main.cpp:203)
2. **Add a new `else if` branch** before the final `else` block
3. **Implement your function logic** using the `operands` span
4. **Return an SExpr** containing your result

#### Example 1: Adding a `max` Function

```cpp
// Add this in the apply_op function before the final else block
else if (op == "max") {
    p_assert(!operands.empty(), "'max' requires at least one argument");
    long result = get_long(operands[0]);
    for (size_t i = 1; i < operands.size(); ++i) {
        long val = get_long(operands[i]);
        if (val > result) {
            result = val;
        }
    }
    return SExpr{Atom{result}};
}
```

After adding this, rebuild with `make` and you can use it:

```
> (max 5 10 3 8)
=> 10
```

#### Example 2: Adding a `mod` (Modulo) Function

```cpp
else if (op == "mod") {
    p_assert(operands.size() == 2, "'mod' requires exactly two arguments");
    long val1 = get_long(operands[0]);
    long val2 = get_long(operands[1]);
    p_assert(val2 != 0, "Modulo by zero");
    return SExpr{Atom{val1 % val2}};
}
```

Usage:

```
> (mod 17 5)
=> 2
```

#### Example 3: Adding a `length` Function for Lists

```cpp
else if (op == "length") {
    p_assert(operands.size() == 1, "'length' requires one argument");
    const auto& arg = operands[0];
    p_assert(arg.list.has_value(), "'length' argument must be a list");
    const auto& list = *arg.list;
    return SExpr{Atom{static_cast<long>(list.size())}};
}
```

Usage:

```
> (length '(1 2 3 4 5))
=> 5
```

#### Important Notes

- **Operands are pre-evaluated**: By the time `apply_op` is called, all arguments have already been evaluated
- **Special forms require different handling**: If you need unevaluated arguments (like `quote`), add handling in the `eval` function instead (around main.cpp:290)
- **Use `p_assert` for validation**: This works at both compile-time and runtime
- **Return `SExpr{Atom{...}}` for numbers**: Wrap your result in the appropriate types
- **Compile-time compatible**: Use only `constexpr`-compatible operations for compile-time support

#### Testing Your New Function

Add compile-time tests in `main()`:

```cpp
// In main() function, after existing tests
constexpr auto val6 = "(max 10 5 20 15)"_lisp;
static_assert(val6 == 20);

constexpr auto val7 = "(mod 17 5)"_lisp;
static_assert(val7 == 2);
```

Then rebuild and test:

```bash
make clean
make test
```

## Implementation Details

### Key Components

1. **FixedString**: Template struct for compile-time string literals
2. **AST Data Structures**:
   - `Atom`: Either a number (`long`) or symbol (`string_view`)
   - `List`: Vector of S-expressions
   - `SExpr`: Union of Atom or List
3. **Parser**: Converts string input to AST
4. **Evaluator**: Recursively evaluates AST using McCarthy's eval rules

### C++20 Features Used

- `constexpr` vectors and algorithms
- `std::span` for non-owning array views
- `std::variant` for sum types
- `std::optional` for nullable values
- `std::transform_reduce` for functional-style reductions
- `consteval` for compile-time-only evaluation
- User-defined literals (UDL) for `_lisp` suffix

## Requirements

- C++20 compliant compiler (clang++ 10+, g++ 10+, or MSVC 2019+)
- Standard library with C++20 support

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Example Code

Here's a minimal example of using the compile-time evaluator:

```cpp
#include "main.cpp"

int main() {
    // Evaluated at compile-time!
    constexpr auto result = "(+ (* 2 3) (- 10 4))"_lisp;
    static_assert(result == 12); // 2*3 + (10-4) = 6 + 6 = 12

    return 0;
}
```
