# Omnikarai Standard Modules

Modules are loaded with `use <name>` and called with dot syntax.
The authoritative list is what the compiler accepts — `omnicc version`
prints it. There are nine built-in modules:

`time` `datetime` `math` `os` `io` `sys` `list` `str` `ai`

> Honesty note (v7.1.0): earlier READMEs advertised a `numrai` module that
> never existed in the compiler. It was removed from all documentation and
> from the module table in this release.

## time

```
use time
set t0 = time.now()
time.ms(t0)      # elapsed milliseconds since t0
time.us(t0)      # elapsed microseconds
time.ns(t0)      # elapsed nanoseconds
time.format(t0)  # human-readable duration
```

## datetime

```
use datetime
datetime.now()        # current wall time
datetime.year(t)      # accessors: year month day hour minute second
datetime.format(t, "%Y-%m-%d")   # strftime-style formatting
datetime.timestamp()  # unix seconds
```

## math

```
math.sqrt(x)   math.pow(b, e)   math.abs(x)     math.floor(x)
math.ceil(x)   math.round(x)    math.sin(x)     math.cos(x)
math.tan(x)    math.log(x)      math.log2(x)    math.log10(x)
math.exp(x)    math.exp2(x)     math.tanh(x)    math.atan(x)
math.atan2(y, x)                math.cbrt(x)
math.pi   math.e   math.tau     math.min_f(a,b)  math.max_f(a,b)
math.itof(i)   math.ftoi(f)     # int↔float conversions
```

Trigonometric/float functions take and return 64-bit floats; integer
arguments are converted automatically.

## os

```
os.exit(code)     os.platform()      os.cwd()
os.getpid()       os.getenv("PATH")  os.exists(path)
os.mkdir(path)    os.remove(path)    os.rename(a, b)
os.isfile(path)   os.isdir(path)     os.abspath(p)
os.basename(p)    os.dirname(p)      os.join(a, b)
```

`os.platform()` returns `"windows"` or `"linux"`.

## io

```
io.read(path)             # whole file as a string
io.write(path, s)         # overwrite
io.append(path, s)        # append
io.exists(path)           # 1 / 0
io.delete(path)           # unlink
io.copy(src, dst)         # byte-exact copy
io.readline(path, n)      # read line n (0-indexed)
io.line_count(path)       # count of lines
io.rename(from, to)
```

## sys

```
sys.version()    # "Omnikarai v7.1.0 (x86-64 Linux)" / "(x86-64 Windows)"
sys.platform()   # "linux-x64" / "windows-x64"
sys.arch()       # "x86_64"
sys.omni_ver()   # "7.1.0"
sys.bits()       # 64
sys.argv()       # command-line arguments
sys.memory()     # total physical RAM in MB
```

## list

```
set a = list.new()
list.push(a, 42)     list.pop(a)        list.get(a, i)
list.set(a, i, v)    list.len(a)        list.free(a)
list.sort(a)         list.reverse(a)    list.copy(a)
list.insert(a, i, v) list.remove(a, i)  list.clear(a)
list.slice(a, i, j)  list.find(a, v)    list.map(a, fn)
list.filter(a, fn)   list.reduce(a, fn, init)
```

Lists print as `[1, 2, 3]`.

## str

```
str.upper(s)   str.lower(s)    str.trim(s)     str.reverse(s)
str.replace(s, a, b)           str.split(s, sep)  str.join(list, sep)
str.contains(s, sub)           str.find(s, sub)   str.count(s, sub)
str.starts_with(s, p)          str.ends_with(s, p)
str.repeat(s, n)               str.pad_left(s, n) str.pad_right(s, n)
str.slice(s, i, j)             str.is_digit(s)    str.is_alpha(s)
str.eq(a, b)                   str.to_int(s)
```

## ai — SIMD kernels (AVX2 with scalar fallback)

The `ai` module works on buffers allocated as FP32 or byte arrays:

```
use ai
set a = ai.alloc(1024)       # 1024 floats, 64-byte aligned, zeroed
ai.set(a, 0, 1.0)            # store float (as IEEE-754 bits)
ai.get(a, 0)                 # load float, returned as int32 bits

# INT8 / UINT8 element accessors (added in v7.1.0 — finding #16):
ai.set_i8(a, 0, 5)           # store one signed byte
ai.set_u8(a, 1, 200)         # store one unsigned byte
ai.get_i8(a, 0)              # load, sign-extended
ai.get_u8(a, 1)              # load, zero-extended

ai.relu(a, n)                # in-place ReLU, VMAXPS
ai.softmax(a, n)             # in-place softmax, AVX2 exp
ai.layernorm(a, n)           # in-place layer normalization
ai.dot(a, b, n)              # FP32 dot product, VFMADD231PS
ai.dot_i8(a, b, n)           # INT8 dot product, VPMADDUBSW
ai.matmul(A, x, y, rows, n)  # matrix × vector, cache-tiled
ai.matmul_nn(C, A, B, m, k, n)  # matrix × matrix
ai.gemm(C, A, B, m, k, n, alpha, beta)
ai.bench_start()             # timing helpers for kernels
ai.bench_end_us(t0)
ai.print(a, n)               # dump buffer
ai.free(a)
```

Important: `ai.set` writes **FP32**. To populate a buffer consumed by
`ai.dot_i8` you must use `ai.set_u8` / `ai.set_i8` — the FP32 bit pattern
of `1.0` is *not* the byte `1`.
