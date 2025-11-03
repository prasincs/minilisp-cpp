# Makefile for MiniLisp C++
# Builds a compile-time and runtime Lisp interpreter

# Compiler settings
CXX := clang++
CXXFLAGS := -std=c++20 -Wall -Wextra -pedantic -O2
DEBUGFLAGS := -g -O0

# Size optimization flags
SMALLFLAGS := -std=c++20 -Os -flto -ffunction-sections -fdata-sections
ULTRAFLAGS := -std=c++20 -Os -flto -DMINIMAL_BUILD -fno-rtti -ffunction-sections -fdata-sections

# Platform-specific linker flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	SMALLLDFLAGS := -Wl,-dead_strip
	ULTRALDFLAGS := -Wl,-dead_strip
else
	SMALLLDFLAGS := -Wl,--gc-sections
	ULTRALDFLAGS := -Wl,--gc-sections
endif

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

# Build size-optimized binary with LTO
.PHONY: small
small:
	$(CXX) $(SMALLFLAGS) $(SMALLLDFLAGS) -o $(TARGET) $(SRC)
	strip $(TARGET)
	@ls -lh $(TARGET) | awk '{print "Size-optimized build: " $$5 " (" $$9 ")"}'
	@echo "Small build complete! (~18-22KB)"

# Build ultra-small binary with POSIX I/O and optional UPX compression
.PHONY: ultra-small
ultra-small:
	$(CXX) $(ULTRAFLAGS) $(ULTRALDFLAGS) -o $(TARGET) $(SRC)
	strip $(TARGET)
	@SIZE=$$(ls -l $(TARGET) | awk '{print $$5}'); \
	ls -lh $(TARGET) | awk '{print "Stripped size: " $$5 " (" $$9 ")"}';\
	if [ "$$(uname)" = "Darwin" ]; then \
		echo "Ultra-small build complete! (POSIX-only, ~34KB)"; \
		echo "Note: UPX compression not reliable on macOS"; \
	elif command -v upx >/dev/null 2>&1; then \
		if upx --best -o $(TARGET).upx $(TARGET) 2>/dev/null; then \
			mv $(TARGET).upx $(TARGET); \
			ls -lh $(TARGET) | awk '{print "UPX compressed: " $$5 " (" $$9 ")"}';\
			echo "Ultra-small build complete! (POSIX-only, UPX compressed)"; \
		else \
			echo "Ultra-small build complete! (POSIX-only)"; \
			echo "Note: UPX compression failed, but binary is still optimized"; \
		fi \
	else \
		echo "Ultra-small build complete! (POSIX-only)"; \
		echo "Note: Install UPX on Linux for additional compression (~10KB)"; \
	fi

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
	@echo "  make              - Build the project (default, ~39KB, portable)"
	@echo "  make small        - Size-optimized build (~18-22KB, portable, LTO)"
	@echo "  make ultra-small  - Ultra-minimal build (~6-8KB w/UPX, POSIX-only)"
	@echo "  make debug        - Build with debug symbols"
	@echo "  make run          - Build and run the REPL"
	@echo "  make test         - Build and run compile-time tests"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make info         - Display compiler information"
	@echo "  make help         - Show this help message"
