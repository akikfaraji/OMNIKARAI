# The Omnikarai Language

Files use the `.ok` extension. Comments start with `#` and run to end of
line. Blocks are indentation-based (no braces): a `:` at the end of a header
line opens a block, and the block ends when the indentation returns.

## Variables

```
set x = 10          # declare or assign
set name = "omni"   # strings are UTF-8 byte strings
set ok = true       # booleans: true / false
set pi = 3.14       # 64-bit floats
set nothing = nil   # nil
```

`set` both declares and assigns. There is no separate declaration form.
Re-assigning inside loops or if-blocks updates the visible variable.

## Arithmetic, comparison, logic

```
+  -  *  /  %       # integers truncate toward zero (C semantics)
**                  # power
==  !=  <  >  <=  >=
and  or  not
```

`+` on strings concatenates; if either side is a string, numbers are
converted automatically:

```
print("n = " + 5)        # n = 5
```

`/` and `%` follow C truncation semantics for negative operands:
`-7 / 4 == -1`, `-7 % 4 == -3`.

## Control flow

```
if x > 0:
    print("pos")
elif x == 0:
    print("zero")
else:
    print("neg")

while i < 10:
    set i = i + 1

break        # leave the innermost loop
continue     # next iteration

for i in range(10):          # 0 .. 9
for i in range(2, 12):       # 2 .. 11
for i in range(0, 12, 3):    # 0, 3, 6, 9
for item in some_list:       # iterate list values
```

## Match

```
match value:
    case 1:
        print("one")
    case 2:
        print("two")
    default:
        print("other")
```

## Functions

```
fn add(a, b):
    return a + b

fn sign(n):
    if n > 0:
        return 1
    return 0 - 1
```

- Parameters are positional; recursion is supported (functions may call
  themselves and each other).
- Functions can be used as values and passed to `list.map`, `list.filter`
  and `list.reduce`.
- Return type inference is static: a function whose returns are strings is
  known to return a string, so `print(greet("x"))` prints text, not a
  pointer.

## Built-in functions (no module needed)

```
print(v)          # print any value
len(s_or_list)    # string length / list length
int(s)            # string to integer
str(v)            # to string
input(prompt)     # read one line
assert(cond, msg) # abort with message when false
range(a, b, c)    # 1/2/3-arg range
type(v)           # "int", "str", "bool", "float", "list", "nil"
min(a, b) max(a, b) abs(x)
```

## Modules

Module functions are called with a dot: `math.sqrt(2)`, `str.upper(s)`,
`ai.dot(a, b, n)`. Load a module explicitly with `use`:

```
use math
use str
```

See [MODULES.md](MODULES.md) for the full module reference.

## The `const` keyword

```
const LIMIT = 100
```

Compile-time integer constants are folded into the generated code.

## Classes (objects)

```
class Person:
    fn init(self, name, age):
        self.name = name
        self.age = age
    fn greet(self):
        return "hi " + self.name

set p = Person("Ada", 36)
p.greet()
```

Fields are stored in a heap array per instance; methods take `self`
explicitly.

## Running and building

```
omnicc run hello.ok        # JIT: compile and execute in-process
omnicc build hello.ok      # standalone executable (embeds runtime + source)
omnicc check hello.ok      # parse + check only
omnicc dump hello.ok       # dump generated x86-64 machine code
```

`omnicc build` produces a copy of the omnicc engine with your program
embedded; the copy recompiles the program in-process at startup, so the
result runs on any machine of the same platform with nothing installed.
(True freestanding binary output is planned — see
[ROADMAP.md](ROADMAP.md) V01.02.)

## Language evolution

This reference describes the language **as implemented**. Planned
additions — the dual memory model, package-system surface, architecture
support — are tracked as design requirements in
[MEMORY_MODEL.md](MEMORY_MODEL.md), [PACKAGE_ECOSYSTEM.md](PACKAGE_ECOSYSTEM.md)
and [PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md), sequenced in
[ROADMAP.md](ROADMAP.md). Nothing is added to this reference until it
exists and is tested.
