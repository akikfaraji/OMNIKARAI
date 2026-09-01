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
# ============================================================

UNAME := $(shell uname -s)

CC      ?= gcc
CFLAGS  ?= -Iinclude -O2 -std=c99 -Wall -Wextra -fno-strict-aliasing -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDLIBS  = -lm

# Native language-runtime kernels: AVX2/FMA where the compiler defines them.
# `make portable` removes them, exercising the scalar fallback paths.
PORTABLE=0
ifeq ($(PORTABLE),1)
  KERNEL_FLAGS =
else
  KERNEL_FLAGS = -O3 -mavx2 -mfma
endif

SOURCES = src/main.c src/lexer.c src/parser.c src/codegen.c src/diag.c
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
	del /Q bin\omnicc.exe 2>NUL

.PHONY: all portable asan windows test clean
