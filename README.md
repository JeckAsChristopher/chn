<p align="center">
  <img src="assets/chn_logo.png" alt="CHN Logo" width="320"/>
</p>

<h1 align="center">CHN 1.0</h1>

<p align="center">
  <a href="https://www.apache.org/licenses/LICENSE-2.0">
    <img src="https://img.shields.io/badge/license-Apache%202.0-blue.svg" alt="License"/>
  </a>
  <img src="https://img.shields.io/badge/version-1.0-00bcd4.svg" alt="Version"/>
  <img src="https://img.shields.io/badge/built%20with-C99-orange.svg" alt="Built with C99"/>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Android%20%7C%20macOS-informational.svg" alt="Platform"/>
  <img src="https://img.shields.io/badge/maintained-yes-brightgreen.svg" alt="Maintained"/>
  <img src="https://img.shields.io/badge/contributions-welcome-blueviolet.svg" alt="Contributions Welcome"/>
</p>

CHN is a fast, lightweight scripting language built entirely in C with its own bytecode virtual machine and zero external dependencies. It is designed to be easy to read and write while remaining close to the metal. Source files compile to a compact bytecode format and run on a stack based VM with a generational garbage collector, tail call optimization, and a full standard library covering files, networking, math, and OS interaction.

CHN runs anywhere POSIX is available. It works on Linux, macOS, and Android including Termux on AArch64 hardware. A single binary is all you need. There is no runtime to install, no package manager to configure, and no virtual environment to activate.

Licensed under the Apache License 2.0. Copyright 2025 Jeck Christopher Anog.

---

## Table of Contents

- [Why CHN](#why-chn)
- [Features](#features)
- [Building](#building)
- [Quick Start](#quick-start)
- [Usage](#usage)
- [Language Overview](#language-overview)
  - [Program Structure](#program-structure)
  - [Comments](#comments)
  - [Variables and Types](#variables-and-types)
  - [Operators](#operators)
  - [Control Flow](#control-flow)
  - [Functions](#functions)
  - [Lambdas and Closures](#lambdas-and-closures)
  - [Arrays](#arrays)
  - [Dicts](#dicts)
  - [Structs](#structs)
  - [Strings and F-Strings](#strings-and-f-strings)
  - [Error Handling](#error-handling)
  - [Imports and Modules](#imports-and-modules)
  - [Visibility](#visibility)
- [Standard Library](#standard-library)
  - [Output and Input](#output-and-input)
  - [OS](#os)
  - [File and Directory](#file-and-directory)
  - [Math](#math)
  - [Networking](#networking)
  - [Binary I/O](#binary-io)
  - [Utility](#utility)
- [Bytecode and CCO Bundles](#bytecode-and-cco-bundles)
- [The REPL](#the-repl)
- [CLI Reference](#cli-reference)
- [Running Tests](#running-tests)
- [Architecture](#architecture)
- [Runtime Limits](#runtime-limits)
- [License](#license)

---

## Why CHN

Most scripting languages carry significant runtime weight. They come with large standard libraries, complex dependency trees, or virtual machines that take time to start. CHN takes the opposite approach. The entire language, compiler, virtual machine, garbage collector, and standard library compile into a single C binary under 200 KB. It starts instantly, uses minimal memory, and has no dependencies beyond libc and libm.

CHN is also honest about what it is. It does not try to be Python or JavaScript. It is a focused scripting language for developers who want to write small tools, automate tasks, build libraries, or explore language implementation without fighting a bloated ecosystem. The syntax is clean, the error messages point at real source locations, and the module system is straightforward.

---

## Features

- Single pass bytecode compiler with three optimization levels
- Stack based virtual machine with tail call optimization
- Generational garbage collector with generational young and old heaps
- Slab allocator for short strings, reducing heap fragmentation
- Weak reference string intern table for deduplication
- First class functions, closures, and anonymous lambdas
- F-string interpolation supporting arbitrary expressions
- try, catch, and throw error handling with nestable scopes
- Three tier module system: source files, compiled bytecode, and CCO bundles
- Function visibility: public, private, and protected
- Comprehensive standard library covering OS, files, math, networking, and binary I/O
- Interactive REPL with AST dump and bytecode disassembly modes
- Optional bytecode minification for smaller distributed bundles
- Runtime argument passing via os::args()
- Colored, location-aware error output

---

## Building

CHN is written in C99 with POSIX extensions. There are no third party dependencies. To build, compile all `.c` files in `src/v1.0/` and link with libm.

On Linux or macOS with gcc:

```sh
gcc -O2 -o chn src/v1.0/*.c -lm
```

On Android or Termux with clang:

```sh
clang -O2 -o chn src/v1.0/*.c -lm
```

On any system with cc:

```sh
cc -O2 -std=c99 -o chn src/v1.0/*.c -lm
```

The output is a single self-contained binary. Copy it to any directory on your PATH and it is ready to use. No installation scripts, no shared libraries, no configuration files.

To make library bundles discoverable, create a `chn-libs/` directory in the same location as the binary. The runtime searches that directory automatically when resolving bare import names.

---

## Quick Start

Save this to `hello.chn`:

```chn
public entry main() {
    stdo("Hello, world!")
}
```

Run it:

```sh
chn hello.chn
```

A slightly more complete example showing variables, a loop, and a function:

```chn
func greet(name) {
    return f"Hello, {name}!"
}

public entry main() {
    let people = ["Alice", "Bob", "Carol"]
    for person in people {
        stdo(greet(person))
    }
}
```

---

## Usage

Run a source file directly:

```sh
chn script.chn
```

Pass arguments to the running program. Everything after the filename is available via `os::args()`:

```sh
chn script.chn --port 8080 input.txt
```

Run a compiled CCO bundle that has an entry point:

```sh
chn app.cco
```

Compile a source file to bytecode:

```sh
chn script.chn -c script.chn2
```

Bundle multiple source files into a single CCO package:

```sh
chn a.chn b.chn c.chn -oc mylib.cco
```

Check syntax without running:

```sh
chn script.chn --check
```

Dump the parsed AST:

```sh
chn script.chn --ast
```

Disassemble compiled bytecode:

```sh
chn script.chn --disasm
```

Start the interactive REPL:

```sh
chn
```

---

## Language Overview

### Program Structure

Every runnable CHN program defines exactly one `public entry main()` function. This function is the program entry point and must be marked both `public` and `entry`. A file without an entry point is treated as a library and can only be imported, not run directly.

```chn
public entry main() {
    stdo("program starts here")
}
```

Functions and variables can be defined before or after `main`. The order of declaration does not matter for top-level functions because the compiler resolves them in a single pass across the full file.

### Comments

Comments begin with `--` and run to the end of the line. There are no block comments.

```chn
-- This entire line is a comment
let x = 10  -- This is an inline comment
```

### Variables and Types

Variables are declared with `let` or `var`. Both keywords are equivalent. All variables are block scoped and type is inferred from the assigned value.

```chn
let count  = 0
let name   = "CHN"
let ratio  = 0.75
let active = true
let empty  = nil
```

CHN has seven built-in value types:

| Type | Description |
|------|-------------|
| `number` | 64 bit IEEE 754 floating point |
| `string` | Immutable UTF-8 text |
| `bool` | `true` or `false` |
| `nil` | Absence of a value |
| `array` | Ordered, resizable sequence |
| `dict` | String keyed hash map |
| `function` | First class callable |

The `typeof` operator returns the type name as a string at runtime:

```chn
stdo(typeof 42)           -- number
stdo(typeof "hello")      -- string
stdo(typeof true)         -- bool
stdo(typeof nil)          -- nil
stdo(typeof [1, 2, 3])    -- array
stdo(typeof {a: 1})       -- dict
stdo(typeof func() {})    -- function
```

Variables can be reassigned freely. A variable initially holding a number can later hold a string. Type checking happens at runtime when an operation is performed.

### Operators

CHN supports a full set of arithmetic, comparison, logical, bitwise, and membership operators.

Arithmetic operators produce numbers. Division always returns a float even when both operands are whole numbers. The `**` operator is exponentiation.

```
+    addition
-    subtraction
*    multiplication
/    division (always float)
%    modulo
**   exponentiation
```

Compound assignment operators modify a variable in place:

```
+=    -=    *=    /=    %=    **=
```

The `++` increment operator works as both prefix and postfix. Note that `--` is not available as a decrement operator because it conflicts with comment syntax. Use `-= 1` instead.

Comparison operators return a bool:

```
==    equal
!=    not equal
<     less than
>     greater than
<=    less than or equal
>=    greater than or equal
```

Logical operators:

```
&&    logical and
||    logical or
!     logical not
```

Bitwise operators work on numbers treated as 32 bit integers:

```
&     bitwise and
|     bitwise or
^     bitwise xor
~     bitwise not
<<    left shift
>>    right shift
```

The `in` operator tests membership. It works on arrays (value present), dicts (key present), and strings (substring present):

```chn
stdo(3 in [1, 2, 3])          -- true
stdo("x" in {x: 1, y: 2})    -- true
stdo("lo" in "hello")         -- true
```

The `not in` form is the negated equivalent.

The `??` null coalescing operator returns the left side if it is not nil, otherwise the right side:

```chn
let port = config.port ?? 8080
```

The ternary operator selects between two values based on a condition:

```chn
let label = score > 50 ? "pass" : "fail"
```

### Control Flow

CHN has if/else, while, do-while, for-in, and switch statements.

If statements evaluate a condition and run the matching block. Braces are required even for single line bodies:

```chn
if x > 100 {
    stdo("large")
} else if x > 50 {
    stdo("medium")
} else {
    stdo("small")
}
```

The while loop runs as long as its condition is true:

```chn
let n = 10
while n > 0 {
    stdo(n)
    n -= 1
}
```

The do-while loop always runs at least once before checking its condition:

```chn
let i = 0
do {
    stdo(i)
    i += 1
} while i < 3
```

The for-in loop iterates over an array or a range. The loop variable is scoped to the loop body:

```chn
for item in ["a", "b", "c"] {
    stdo(item)
}

for i in range(5) {
    stdo(i)
}
```

`range()` generates a sequence of numbers. It accepts one, two, or three arguments:

```chn
range(5)          -- 0, 1, 2, 3, 4
range(2, 7)       -- 2, 3, 4, 5, 6
range(0, 10, 2)   -- 0, 2, 4, 6, 8
```

The switch statement compares a value against a series of cases and runs the first matching block. A `default` block runs when nothing matches:

```chn
switch status {
    case 200:
        stdo("OK")
    case 404:
        stdo("Not Found")
    case 500:
        stdo("Server Error")
    default:
        stdo("Unknown")
}
```

`break` exits a loop immediately. `continue` skips the rest of the current iteration and moves to the next. Both work inside `while`, `do-while`, and `for-in` loops.

### Functions

Functions are declared with the `func` keyword followed by a name, a parameter list, and a body block. Parameters have no type annotations. Return values are inferred.

```chn
func add(a, b) {
    return a + b
}

stdo(add(3, 7))   -- 10
```

Functions are first class values. They can be stored in variables, passed as arguments, and returned from other functions:

```chn
func apply(f, value) {
    return f(value)
}

func triple(x) { return x * 3 }

stdo(apply(triple, 5))   -- 15
```

Recursive functions work naturally. The compiler recognizes direct tail calls and optimizes them to avoid growing the call stack:

```chn
func factorial(n) {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

func sum_to(n, acc) {
    if n == 0 { return acc }
    return sum_to(n - 1, acc + n)   -- tail call, stack does not grow
}
```

Functions can be nested. An inner function has access to the outer function's variables:

```chn
func outer(x) {
    func inner(y) {
        return x + y
    }
    return inner(10)
}

stdo(outer(5))   -- 15
```

### Lambdas and Closures

Anonymous functions are written with `func` without a name, or with the arrow shorthand for single expression bodies.

Full anonymous form:

```chn
let square = func(x) { return x * x }
stdo(square(9))   -- 81
```

Arrow form, single parameter:

```chn
let double = x -> x * 2
```

Arrow form, multiple parameters:

```chn
let add = (a, b) -> a + b
```

Lambdas close over variables from the surrounding scope. The captured value is shared by reference:

```chn
func make_counter() {
    let count = 0
    return func() {
        count += 1
        return count
    }
}

let c = make_counter()
stdo(c())   -- 1
stdo(c())   -- 2
stdo(c())   -- 3
```

Lambdas work naturally with array higher-order methods:

```chn
let nums = [1, 2, 3, 4, 5, 6]
let evens  = nums.filter(x -> x % 2 == 0)
let doubled = evens.map(x -> x * 2)
stdo(doubled)   -- [4, 8, 12]
```

### Arrays

Arrays are ordered, resizable sequences that hold values of any type including mixed types and nested arrays. Index access is zero based.

```chn
let fruits = ["apple", "banana", "cherry"]
stdo(fruits[0])          -- apple
stdo(fruits.length())    -- 3
fruits.push("date")
stdo(fruits.length())    -- 4
```

Arrays support a large set of built-in methods:

| Method | Description |
|--------|-------------|
| `length()` | number of elements |
| `push(v)` / `add(v)` / `append(v)` | append to end |
| `pop()` | remove and return last element |
| `shift()` | remove and return first element |
| `insert(i, v)` | insert value at index |
| `cut(i)` | remove element at index |
| `remove(v)` | remove first occurrence of value |
| `rall(v)` | remove all occurrences of value |
| `contains(v)` | true if value is present |
| `index_of(v)` | index of first occurrence or -1 |
| `sort()` | sort in place |
| `reverse()` | reverse in place |
| `copy()` | shallow copy |
| `fill(v)` | fill all slots with value |
| `join(sep)` | concatenate elements into a string |
| `map(f)` | return new array with function applied to each element |
| `filter(f)` | return new array with only elements where function returns true |
| `reduce(f, init)` | fold elements into a single value |
| `flat()` | flatten one level of nesting |
| `unique()` | return deduplicated copy |
| `first()` | first element |
| `last()` | last element |
| `sum()` | sum of all numeric elements |
| `min()` | minimum numeric value |
| `max()` | maximum numeric value |
| `any(f)` | true if any element satisfies predicate |
| `all(f)` | true if all elements satisfy predicate |
| `count(v)` | count occurrences of value |
| `slice(start, len)` / `sub(start, len)` | return subarray |

### Dicts

Dicts are unordered string-keyed hash maps. They are created with `{}` literal syntax using bare identifier keys or string keys followed by colon-separated values.

```chn
let config = {
    host: "localhost",
    port: 5432,
    ssl: false
}

stdo(config.host)    -- localhost
config.port = 3306
config.name = "mydb"
```

Nested dicts work naturally:

```chn
let app = {
    server: { host: "0.0.0.0", port: 8080 },
    db:     { host: "localhost", port: 5432 }
}

stdo(app.server.port)   -- 8080
```

Dict methods:

| Method | Description |
|--------|-------------|
| `has(key)` | true if key exists |
| `keys()` | array of all keys |
| `values()` | array of all values |
| `get(key)` | get value by key string |
| `delete(key)` | remove a key |
| `size()` | number of key-value pairs |
| `merge(other)` | copy all keys from another dict |
| `to_arr()` | convert to array of two-element arrays |

### Structs

The `struct` keyword defines a named constructor function that creates a dict with named fields and default values. Positional arguments are assigned to fields in declaration order.

```chn
struct Vector3 {
    x: 0,
    y: 0,
    z: 0
}

let pos = Vector3(1, 2, 3)
stdo(pos.x)   -- 1
stdo(pos.y)   -- 2
stdo(pos.z)   -- 3
```

The result is a plain dict so all dict methods apply. Structs are a convenience for constructing typed records without boilerplate.

```chn
struct Player {
    name: "unnamed",
    hp:   100,
    mp:   50
}

let hero = Player("Arthur", 200, 80)
hero.hp -= 30
stdo(f"{hero.name} has {hero.hp} HP")
```

### Strings and F-Strings

Strings are immutable UTF-8 sequences. Concatenation uses `+`. Individual characters are accessed with index notation.

```chn
let s = "Hello, CHN"
stdo(s[0])         -- H
stdo(s.length())   -- 10
stdo(s.upper())    -- HELLO, CHN
```

F-strings are string literals prefixed with `f`. Any expression wrapped in `{}` inside an f-string is evaluated and converted to a string at runtime.

```chn
let name = "world"
let n    = 42
stdo(f"Hello, {name}!")
stdo(f"The answer is {n}")
stdo(f"Double: {n * 2}")
stdo(f"Type: {typeof name}")
```

String methods:

| Method | Description |
|--------|-------------|
| `length()` | character count |
| `upper()` | uppercase copy |
| `lower()` | lowercase copy |
| `trim()` | strip leading and trailing whitespace |
| `split(sep)` | split on separator, return array |
| `contains(sub)` | true if substring is present |
| `starts_with(s)` | true if string begins with s |
| `ends_with(s)` | true if string ends with s |
| `replace(old, new)` | replace first occurrence |
| `find(sub)` | index of first occurrence or -1 |
| `slice(start, len)` / `sub(start, len)` | return substring |
| `reverse()` | reversed copy |
| `to_num()` | parse string as number |
| `pad_left(width, ch)` | left pad to width with character |
| `pad_right(width, ch)` | right pad to width with character |
| `repeat(n)` | concatenate string with itself n times |
| `char_at(i)` | single character at index |
| `count(sub)` | count non-overlapping occurrences |

### Error Handling

CHN uses `try`, `catch`, and `throw` for structured error handling. Any value can be thrown including strings, numbers, and dicts. The caught value is bound to the variable in the catch clause.

```chn
func divide(a, b) {
    if b == 0 { throw "division by zero" }
    return a / b
}

try {
    stdo(divide(10, 2))    -- 5
    stdo(divide(10, 0))    -- throws
} catch err {
    stdo(f"error: {err}")
}
```

Throwing a dict gives structured error information:

```chn
func fetch(url) {
    if !url.starts_with("http") {
        throw { code: "INVALID_URL", message: f"bad url: {url}" }
    }
}

try {
    fetch("ftp://bad")
} catch e {
    stdo(e.code)
    stdo(e.message)
}
```

Try blocks can be nested. A throw inside a catch re-throws and is caught by the next outer handler:

```chn
try {
    try {
        throw "first"
    } catch e {
        stdo(f"inner caught: {e}")
        throw f"wrapped: {e}"
    }
} catch e {
    stdo(f"outer caught: {e}")
}
```

### Imports and Modules

CHN resolves imports at compile time. There are three forms.

Import a module by bare name. The runtime searches the lib paths automatically:

```chn
imp mathlib
imp utils
```

Import a module by relative path:

```chn
imp ./helpers.chn
imp ../shared/types.chn
```

Import a precompiled CCO bundle:

```chn
imp::lib mathlib
imp::lib ./bundles/graphics
```

Module search order for bare names:

1. Same directory as the importing source file
2. `chn-libs/` walking up from the importing file toward the filesystem root
3. `chn-libs/` beside the CHN binary

Functions must be explicitly marked `export` to be visible to importers:

```chn
export func clamp(x, lo, hi) {
    if x < lo { return lo }
    if x > hi { return hi }
    return x
}

export func lerp(a, b, t) {
    return a + (b - a) * t
}

-- private, not visible outside this file
func internal_helper(x) {
    return x * x
}
```

Each file is loaded at most once per program run. Circular imports are detected and prevented automatically.

### Visibility

Functions have three access levels that control where they can be called from.

| Keyword | Access |
|---------|--------|
| `public` | callable from any file |
| `private` | only callable within the same source file |
| `protected` | callable within the same CCO bundle but not from outside it |
| (none) | defaults to private |

The `entry` keyword designates the program entry point and must always be combined with `public`:

```chn
public entry main() {
    stdo("starting")
}
```

Visibility is enforced at compile time. Calling a private function from another file is a compile error.

---

## Standard Library

### Output and Input

```chn
stdo(value)          -- print value followed by newline
stdi("prompt: ")     -- print without newline, used for prompts
let line = input()   -- read one line from stdin, returns string
```

### OS

```chn
os::time()                  -- Unix timestamp as number
os::clock()                 -- monotonic clock in seconds
os::sleep(ms)               -- sleep for milliseconds
os::exit(code)              -- terminate with exit code
os::getenv("HOME")          -- read environment variable
os::setenv("KEY", "value")  -- set environment variable
os::unsetenv("KEY")         -- unset environment variable
os::args()                  -- runtime arguments as array of strings
os::platform()              -- "linux", "mac", or "windows"
os::hostname()              -- system hostname as string
os::pid()                   -- current process ID as number
os::system("ls -la")        -- run shell command, return exit code
os::chdir("/tmp")           -- change working directory
os::getcwd()                -- get current working directory
```

### File and Directory

```chn
file::read("path")             -- read entire file as string
file::write("path", text)      -- write string to file, overwrite
file::append("path", text)     -- append string to file
file::exists("path")           -- true if file exists
file::delete("path")           -- delete file, return bool
file::size("path")             -- file size in bytes
file::lines("path")            -- read file into array of lines
file::copy("src", "dst")       -- copy file
file::rename("old", "new")     -- rename or move file
file::move("old", "new")       -- alias for rename
file::perms("path", 0755)      -- set file permissions

dir::list("path")              -- list directory, return array of names
dir::make("path")              -- create directory
dir::exists("path")            -- true if directory exists
dir::remove("path")            -- remove empty directory
```

### Math

```chn
math::floor(x)          -- round down
math::ceil(x)           -- round up
math::round(x)          -- round to nearest
math::trunc(x)          -- truncate toward zero
math::abs(x)            -- absolute value
math::sign(x)           -- -1, 0, or 1
math::sqrt(x)           -- square root
math::pow(x, y)         -- x to the power y
math::log(x)            -- natural logarithm
math::log(x, base)      -- logarithm with arbitrary base
math::log10(x)          -- base-10 logarithm
math::sin(x)            -- sine (radians)
math::cos(x)            -- cosine (radians)
math::tan(x)            -- tangent (radians)
math::atan2(y, x)       -- arctangent of y/x
math::clamp(v, lo, hi)  -- constrain v to [lo, hi]
math::lerp(a, b, t)     -- linear interpolation
math::min(a, b)         -- smaller of two values
math::max(a, b)         -- larger of two values
math::random()          -- random float in [0, 1)
math::random(n)         -- random float in [0, n)
math::pi()              -- 3.14159265358979...
math::e()               -- 2.71828182845904...
math::is_nan(x)         -- true if x is NaN
math::is_inf(x)         -- true if x is infinite
```

### Networking

```chn
net::tcp_listen(port)                     -- open TCP listener, return socket
net::tcp_accept(sock)                     -- accept incoming connection
net::tcp_connect(host, port)              -- connect to remote host
net::send(sock, data)                     -- send string data
net::recv(sock, max_bytes)                -- receive up to max_bytes
net::close(sock)                          -- close socket
net::dns(hostname)                        -- resolve hostname to IP
net::peer_addr(sock)                      -- get remote address string
net::set_timeout(sock, ms)                -- set read/write timeout

net::udp_bind(port)                       -- bind UDP socket
net::udp_send(sock, host, port, data)     -- send UDP datagram
net::udp_recv(sock, max_bytes)            -- receive UDP datagram

net::http_get(url)                        -- perform HTTP GET, return body
net::http_post(url, body)                 -- perform HTTP POST, return body

net::tls_listen(port)                     -- open TLS listener
net::tls_accept(sock)                     -- accept TLS connection
net::tls_connect(host, port)              -- connect with TLS
net::tls_send(sock, data)                 -- send over TLS
net::tls_recv(sock, max_bytes)            -- receive over TLS
net::tls_close(sock)                      -- close TLS connection
```

### Binary I/O

```chn
bin::write("path", array_of_bytes)   -- write byte array to file
bin::read("path")                    -- read file as array of byte values
bin::write_num("path", n)            -- write number as raw bytes
```

### Utility

```chn
str(value)               -- convert any value to its string representation
len(value)               -- length of array, string, or dict
range(stop)              -- generate 0 to stop-1
range(start, stop)       -- generate start to stop-1
range(start, stop, step) -- generate with step size
```

---

## Bytecode and CCO Bundles

CHN has three compiled artifact formats that serve different distribution needs.

**`.chn2` program bytecode** is a single compiled program file containing the entry chunk, embedded function table, and a record of any imports it requires. It is produced with `-c`. It can be executed directly but cannot be recompiled or disassembled back to source.

**`.function` function bundle** contains only exported functions and no entry point. It is produced with `-c --func`. This format is useful for distributing a small set of utility functions without the overhead of a full CCO container.

**`.cco` CHN Compiled Object** is the primary distribution format. It packs one or more compiled source units into a single binary container. Units within the same CCO can reference each other regardless of visibility through bundle-internal linkage. If any unit contains a `public entry main()`, the CCO can be run directly. Otherwise it is a library and must be imported with `imp::lib`.

CCO files embed their own import dependency list. When a CCO is executed, the runtime reads this list and resolves all required CCOs before running the entry chunk. This means you can distribute a CCO and its dependencies as a flat directory and they will all wire up automatically.

Build a library CCO:

```sh
chn mathlib.chn -oc mathlib.cco
```

Build a runnable application CCO that imports the library:

```sh
chn app.chn -oc app.cco
chn app.cco
```

Use `--no-minify` to produce human-inspectable bytecode for debugging:

```sh
chn mathlib.chn --no-minify -oc mathlib.cco
```

---

## The REPL

Running `chn` with no arguments starts the interactive REPL. Expressions and statements are executed immediately after you press Enter. The REPL maintains state between lines within the same session.

```
CHN 1.0  --  :q to quit  :ast/:dis to debug
chn> let x = 10
chn> stdo(x * x)
100
chn> :q
```

REPL commands:

| Command | Effect |
|---------|--------|
| `:q` or `exit` or `quit` | exit the REPL |
| `:ast <expr>` | print the AST for an expression |
| `:dis <expr>` | disassemble an expression to bytecode |
| `:gc` | print garbage collector statistics |

---

## CLI Reference

```
chn <file.chn>                     run source file
chn <file.chn> [args...]           run with runtime arguments
chn <file.cco>                     run CCO with entry main
chn <file.chn2>                    run compiled bytecode
chn <file.chn> -c <out.chn2>       compile to bytecode
chn <file.chn> -c                  compile to <file>.chn2
chn a.chn b.chn -oc out.cco        bundle into CCO
chn                                start REPL

Flags:
  -O0 / -O1 / -O2      optimization level (default -O2)
  --check  / -k        syntax check, no execution
  --disasm / -d        disassemble bytecode to stdout
  --ast    / -a        dump AST to stdout
  --func   / -f        compile to function bundle
  --no-minify          disable bytecode minification
  --no-color           disable colored output
  --color              force colored output
  --version / -v       print version string
  --help    / -h       print usage
```

When running a source file, everything after the filename is passed to the program as runtime arguments via `os::args()`. CHN flags like `-O2` or `-c` that appear after the filename are still processed by CHN, not passed as runtime args. Use `--` to force everything after it to become runtime args:

```sh
chn script.chn -- --version --help
```

---

## Running Tests

The test suite lives at `docs/tests/test_all.sh`. It requires bash and a built `chn` binary. Tests cover arithmetic, string operations, control flow, function calls, closures, arrays, dicts, structs, error handling, switch statements, runtime arguments, CCO compilation, multi-level CCO import chains, and bytecode minification.

Run with the binary on your PATH:

```sh
bash docs/tests/test_all.sh
```

Or point at a specific binary:

```sh
bash docs/tests/test_all.sh ./chn
```

The script prints `PASS` or `FAIL` for each test case. On failure it shows the expected and actual output. The process exits with code 0 if all tests pass and code 1 if any fail.

---

## Architecture

The implementation lives entirely in `src/v1.0/` and is split into focused modules.

| Module | Role |
|--------|------|
| `lexer.c` / `lexer.h` | Tokenizes source text into a token stream |
| `parser.c` / `parser.h` | Recursive descent parser that produces an AST |
| `ast.c` / `ast.h` | AST node types and pretty printer |
| `compiler.c` / `compiler.h` | Single pass compiler from AST to bytecode chunks |
| `vm.c` / `vm.h` | Stack based virtual machine that executes bytecode |
| `gc.c` / `gc.h` | Generational garbage collector with slab allocator and intern table |
| `func.c` / `func.h` | Function object definition and global function registry |
| `bytecode.c` / `bytecode.h` | Serialization and deserialization for `.chn2`, `.function`, and `.cco` |
| `native.c` / `native.h` | OS, file, math, binary, and utility native dispatch |
| `native_net.c` | TCP, UDP, TLS, HTTP native functions |
| `error.c` / `error.h` | Source-location-aware error reporting |
| `common.h` | Shared value model, opcode table, method dispatch, constants |
| `main.c` | CLI parsing, import resolution, REPL, program entry |

The VM maintains a flat value stack and a separate call frame stack. Each call frame stores the executing function, the current instruction pointer, and the base index into the value stack for that frame's locals. A parallel try-frame stack tracks active error handlers.

The garbage collector runs generationally. Newly allocated objects enter the young generation. Objects that survive a configurable number of minor collections are promoted to the old generation. Small strings under 128 bytes are carved from 64 KB memory slabs, avoiding per-string heap allocations. A weak-reference intern table deduplicates short strings so identical string values share a single allocation. Write barriers keep inter-generational references consistent during minor collections.

The bytecode format uses a section-based layout. Each file begins with a magic number and version field, followed by length-prefixed sections for imports, exports, function bodies, the entry chunk, debug symbols, and bundle-internal functions. The CCO container wraps multiple of these compiled units with a header that records unit count, which unit contains the entry point, and flags.

---

## Runtime Limits

These limits are compile-time constants defined in `common.h`. They can be changed and the project recompiled to suit larger workloads.

| Limit | Value |
|-------|-------|
| Max identifier length | 256 characters |
| Max string literal length | 65536 characters |
| Max function parameters | 64 |
| Max call stack depth | 512 frames |
| Max value stack size | 8192 values |
| Max global variables | 1024 |
| Max locals per function | 512 |
| Max functions per compiler unit | 512 |
| Max try handler nesting depth | 64 |
| Max break/continue patch sites | 256 |
| Max imported files per program | 256 |
| Max source files per CCO bundle | 64 |

---

## License

CHN is licensed under the Apache License, Version 2.0.

```
Copyright 2025 Jeck Christopher Anog

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```
