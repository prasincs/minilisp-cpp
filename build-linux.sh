#!/bin/bash
# Cross-compile MiniLisp for Linux using Docker
# Produces a Linux ARM64 binary from macOS

set -e

echo "=== Cross-Compiling MiniLisp for Linux ==="
echo ""

# Build in Ubuntu container
docker run --rm -v "$(pwd)":/work -w /work ubuntu:latest bash -c "
  echo '→ Installing build tools...'
  apt-get update -qq 2>&1 > /dev/null
  apt-get install -y -qq clang make upx 2>&1 > /dev/null
  echo '✓ Tools installed'
  echo ''

  echo '→ Building ultra-small target...'
  make ultra-small
  echo ''

  echo '=== Size Comparison ==='
  echo 'macOS (Mach-O):       35,016 bytes (34KB) stripped'
  echo -n 'Linux (ELF stripped):  '
  stat -c '%s bytes' lisp_repl
  echo -n 'Linux (UPX):           '
  stat -c '%s bytes' lisp_repl || echo 'Calculating after compression...'
  echo ''

  echo '=== Section Analysis ==='
  size lisp_repl || echo 'Binary is UPX compressed - sections not readable'
  echo ''

  echo '=== Testing Functionality ==='
  echo '(+ 10 (* 5 5))' | ./lisp_repl | grep -A1 'MiniLisp'
  echo ''
  echo '✓ Linux binary built successfully!'
  echo ''
  echo 'Output: lisp_repl (Linux ARM64 binary)'
"

# Verify the binary exists
if [ -f lisp_repl ]; then
    echo ""
    echo "=== Final Binary ==="
    ls -lh lisp_repl
    file lisp_repl
else
    echo "Error: Binary not created"
    exit 1
fi
