# ============================================================
#  Omnikarai build system
#
#  Targets:
#    make            build native omnicc (default)
#    make portable   build without AVX2/FMA (scalar fallback kernels)
#    make asan       build with AddressSanitizer + UBSan (debug)
#    make windows    cross-compile with MinGW-w64 (if installed)
#    make test       build + run the portable test suite (tests/run_tests.py)
#    make clean
#
#  Requirements: gcc or clang, C99, POSIX or Windows.
#  AVX2 note: the default native build enables AVX2+FMA language
#  runtime kernels. Running that binary on a pre-Haswell (2013) CPU
#  is unsupported — use `make portable` there. The compiler itself
#  does not require AVX2.
#  Architecture note: AVX2/FMA are x86-only ISA flags. On any other
#  host (e.g. AArch64 Linux / Termux) the Makefile automatically
#  builds the scalar-fallback kernel tier — the supported portable
#  tier (docs/PLATFORM_SUPPORT.md). No flags need to be passed.
# ============================================================

UNAME   := $(shell uname -s)
UNAME_M := $(shell uname -m)

CC      ?= gcc
CFLAGS  ?= -Iinclude -O2 -std=c99 -Wall -Wextra -fno-strict-aliasing -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDLIBS  = -lm

# Native language-runtime kernels: AVX2/FMA where the compiler defines them.
# `make portable` removes them, exercising the scalar fallback paths.
# A non-x86-64 host (aarch64, arm64, riscv64, ...) cannot take -mavx2/-mfma
# at all, so it silently gets the scalar tier — identical to `make portable`.
PORTABLE=0
ifeq ($(PORTABLE),1)
  KERNEL_FLAGS =
else ifeq ($(filter x86_64 amd64,$(UNAME_M)),)
  KERNEL_FLAGS =
else
  KERNEL_FLAGS = -O3 -mavx2 -mfma
endif

SOURCES = src/main.c src/lexer.c src/parser.c src/codegen.c src/diag.c src/omni_mem.c
OMNICC  = bin/omnicc

all: $(OMNICC)

$(OMNICC): $(SOURCES) include/*.h | bin
	$(CC) $(CFLAGS) $(KERNEL_FLAGS) -o $@ $(SOURCES) $(LDLIBS)

portable:
	@$(MAKE) --no-print-directory PORTABLE=1 $(OMNICC)

asan:
	@$(MAKE) --no-print-directory CFLAGS="-Iinclude -O1 -g -std=c99 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all" $(OMNICC)

# Kernel flags during sanitizer build (no AVX2 to keep stacks small)
asan: KERNEL_FLAGS =

bin:
	mkdir -p bin

windows:
	x86_64-w64-mingw32-gcc $(CFLAGS) -O2 -o bin/omnicc.exe $(SOURCES) -lkernel32 -lm

test: $(OMNICC)
	python3 tests/run_tests.py
	python3 tests/check_version.py
	python3 tests/run_regression.py

clean:
	rm -rf bin
ifeq ($(OS),Windows_NT)
	del /Q bin\omnicc.exe 2>NUL
endif

.PHONY: all portable asan windows test clean
