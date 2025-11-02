# Makefile for MiniLisp C++
# Builds a compile-time and runtime Lisp interpreter

# Compiler settings
CXX := clang++
CXXFLAGS := -std=c++20 -Wall -Wextra -pedantic -O2
DEBUGFLAGS := -g -O0

# Target executable
TARGET := lisp_repl
SRC := main.cpp

# Default target
.PHONY: all
all: $(TARGET)

# Build the main executable
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<
	@echo "Build successful! Run './$(TARGET)' to start the REPL."

# Build with debug symbols
.PHONY: debug
debug: CXXFLAGS := -std=c++20 -Wall -Wextra -pedantic $(DEBUGFLAGS)
debug: $(TARGET)
	@echo "Debug build complete!"

# Run the REPL
.PHONY: run
run: $(TARGET)
	./$(TARGET)

# Build and run with example tests
.PHONY: test
test: $(TARGET)
	@echo "Running compile-time tests (built into executable)..."
	./$(TARGET) < /dev/null
	@echo "All tests passed!"

# Clean build artifacts
.PHONY: clean
clean:
	rm -f $(TARGET)
	@echo "Clean complete!"

# Display compiler and environment info
.PHONY: info
info:
	@echo "Compiler: $(CXX)"
	@$(CXX) --version
	@echo "Flags: $(CXXFLAGS)"
	@echo "Target: $(TARGET)"
	@echo "Source: $(SRC)"

# Help target
.PHONY: help
help:
	@echo "MiniLisp C++ Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  make          - Build the project (default)"
	@echo "  make debug    - Build with debug symbols"
	@echo "  make run      - Build and run the REPL"
	@echo "  make test     - Build and run compile-time tests"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make info     - Display compiler information"
	@echo "  make help     - Show this help message"
