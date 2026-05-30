<p align="center">
  <img src="assets/chn_logo.png" alt="CHN Logo" width="320"/>
</p>

<h1 align="center">CHN 1.1</h1>

<p align="center">
  <a href="https://www.apache.org/licenses/LICENSE-2.0">
    <img src="https://img.shields.io/badge/license-Apache%202.0-blue.svg" alt="License"/>
  </a>
  <img src="https://img.shields.io/badge/version-1.1-00bcd4.svg" alt="Version"/>
  <img src="https://img.shields.io/badge/built%20with-C99-orange.svg" alt="Built with C99"/>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Android%20%7C%20macOS-informational.svg" alt="Platform"/>
</p>

CHN is a scripting language written in C99. it compiles source files to bytecode and runs them on a stack-based virtual machine with a generational garbage collector. the entire implementation -- lexer, parser, compiler, VM, GC, and standard library -- fits in a single binary under 300 KB with no external dependencies beyond libc and libm.

version 1.1 adds a generational GC with a slab allocator for short strings, a string intern table, a three-tier module system, function visibility levels, a full networking library with TLS support, binary I/O, and a C++ memory layer with RAII guards around the VM and GC.

licensed under the Apache License 2.0. Copyright 2025 Jeck Christopher Anog.

---

## Table of Contents

- [Why CHN](#why-chn)
- [What Changed in 1.1](#what-changed-in-11)
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

most scripting languages carry significant runtime weight -- large standard libraries, dependency trees, or VMs that take time to spin up. CHN takes the opposite approach. the whole language compiles into a single C binary, starts instantly, uses minimal memory, and has no installation step. copy the binary to your PATH and its ready.

CHN does not try to be Python or JavaScript. its a focused tool for writing small scripts, automating tasks, building distributable library bundles, or learning how a bytecode language works from the inside out. the syntax is clean, error messages point at real source locations, and the module system is straightforward.

---

## What Changed in 1.1

version 1.0 shipped a working bytecode compiler and VM. version 1.1 expands the runtime significantly.

**GC rewrite** -- the garbage collector is now generational. newly allocated objects enter a young generation. objects that survive enough minor collections get promoted to old. short strings (under 128 bytes) are carved from 64 KB slab arenas instead of individual heap allocations. a weak-reference intern table deduplicates string values so identical strings share one allocation.

**C++ memory layer** -- `src/memory/` adds a C++ wrapper around the C core. `MemoryHandler` exposes GC stats and memory reporting. `RAII.h` provides `MallocGuard`, `ChunkGuard`, `VMGuard`, and `defer` so the REPL and embedding code can manage resources without manual cleanup on every exit path.

**Networking** -- `native_net.c` adds TCP, UDP, HTTP, and TLS native functions. the TLS layer wraps OpenSSL or a platform TLS provider. on systems without TLS support, `native_net_stub.c` provides stub implementations that return errors.

**Binary I/O** -- `bin::read` and `bin::write` let scripts read and write raw byte arrays to files.

**Module system** -- three import forms are now supported: bare name, relative path, and `imp::lib` for precompiled CCO bundles. each file is loaded at most once. circular imports are detected and blocked.

**Function visibility** -- `public`, `private`, `protected`, and `export` keywords control cross-file and cross-bundle access. visibility is enforced at compile time.

**Bytecode formats** -- three artifact types: `.chn2` for compiled programs, `.function` for exported function bundles, and `.cco` for multi-unit compiled objects. CCO files embed their dependency list so the runtime can resolve them automatically.

**REPL** -- the REPL now supports multi-line input with bracket depth tracking, persistent history in `~/.chn_history`, `:load`, `:ast`, `:dis`, `:gc`, `:mem`, `:time`, and `:reset` commands, and optional readline integration.

---

## Building

CHN is written in C99 with POSIX extensions. version 1.1 has a C++ wrapper layer (C++11 or later) for the REPL and memory management. the core language files are plain C99.

**build the full binary with CMake (recommended for 1.1):**

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

the CMake build links the C99 core, the C++ memory layer, and optionally readline and OpenSSL if found on the system.

**build just the C99 core (no REPL, no TLS):**

on Linux or macOS with gcc:

```sh
gcc -O2 -o chn src/v1.1/*.c -lm
```

on Android or Termux with clang:

```sh
clang -O2 -o chn src/v1.1/*.c -lm
```

this produces a working binary without the enhanced REPL and without TLS. networking functions that require TLS will return an error at runtime.

**to enable readline:**

```sh
gcc -O2 -DHAVE_READLINE -o chn src/v1.1/*.c src/memory/*.cpp -lm -lreadline -lstdc++
```

**library discovery** -- the runtime searches a `chn-libs/` directory when resolving bare import names. create one next to the binary or in any parent directory of your source files.

---

## Quick Start

save this to `hello.chn`:

```chn
public entry main() {
    stdo("Hello, world!")
}
```

run it:

```sh
chn hello.chn
```

a slightly more complete example:

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

run a source file directly:

```sh
chn script.chn
```

pass arguments to the running program. everything after the filename is available via `os::args()`:

```sh
chn script.chn --port 8080 input.txt
```

run a compiled CCO bundle that has an entry point:

```sh
chn app.cco
```

compile a source file to bytecode:

```sh
chn script.chn -c script.chn2
```

bundle multiple source files into a single CCO package:

```sh
chn a.chn b.chn c.chn -oc mylib.cco
```

check syntax without running:

```sh
chn script.chn --check
```

dump the parsed AST:

```sh
chn script.chn --ast
```

disassemble compiled bytecode:

```sh
chn script.chn --disasm
```

start the interactive REPL:

```sh
chn
```

---

## Language Overview

### Program Structure

every runnable CHN program defines exactly one `public entry main()` function. a file without an entry point is treated as a library and can only be imported.

```chn
public entry main() {
    stdo("program starts here")
}
```

functions and variables can be defined before or after `main`. the compiler resolves top-level names in a single pass across the full file so declaration order does not matter.

### Comments

comments begin with `--` and run to end of line. there are no block comments.

```chn
-- this entire line is a comment
let x = 10  -- inline comment
```

### Variables and Types

variables are declared with `let` or `var`. both keywords are equivalent. all variables are block-scoped and type is inferred from the assigned value.

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
| `number` | 64-bit IEEE 754 floating point |
| `string` | immutable UTF-8 text |
| `bool` | `true` or `false` |
| `nil` | absence of a value |
| `array` | ordered, resizable sequence |
| `dict` | string-keyed hash map |
| `function` | first-class callable |

the `typeof` operator returns the type name as a string at runtime:

```chn
stdo(typeof 42)           -- number
stdo(typeof "hello")      -- string
stdo(typeof true)         -- bool
stdo(typeof nil)          -- nil
stdo(typeof [1, 2, 3])    -- array
stdo(typeof {a: 1})       -- dict
stdo(typeof func() {})    -- function
```

### Operators

CHN supports arithmetic, comparison, logical, bitwise, and membership operators.

arithmetic operators:

```
+    addition
-    subtraction
*    multiplication
/    division (always float)
%    modulo
**   exponentiation
```

compound assignment: `+=  -=  *=  /=  %=  **=`

the `++` increment operator works as prefix and postfix. `--` is not available as a decrement operator because it conflicts with comment syntax. use `-= 1` instead.

comparison operators return a bool:

```
==    !=    <    >    <=    >=
```

logical operators:

```
&&    ||    !
```

bitwise operators work on numbers treated as 32-bit integers:

```
&     |     ^     ~     <<    >>
```

the `in` operator tests membership in arrays, dicts, and strings:

```chn
stdo(3 in [1, 2, 3])          -- true
stdo("x" in {x: 1, y: 2})    -- true
stdo("lo" in "hello")         -- true
```

the `??` null coalescing operator returns the left side if not nil, otherwise the right:

```chn
let port = config.port ?? 8080
```

ternary operator:

```chn
let label = score > 50 ? "pass" : "fail"
```

### Control Flow

CHN has if/else, while, do-while, for-in, and switch. braces are required on all blocks.

```chn
if x > 100 {
    stdo("large")
} else if x > 50 {
    stdo("medium")
} else {
    stdo("small")
}
```

```chn
let n = 10
while n > 0 {
    stdo(n)
    n -= 1
}
```

```chn
let i = 0
do {
    stdo(i)
    i += 1
} while i < 3
```

for-in loop iterates over an array or a range:

```chn
for item in ["a", "b", "c"] {
    stdo(item)
}

for i in range(5) {
    stdo(i)
}
```

`range()` accepts one, two, or three arguments:

```chn
range(5)          -- 0, 1, 2, 3, 4
range(2, 7)       -- 2, 3, 4, 5, 6
range(0, 10, 2)   -- 0, 2, 4, 6, 8
```

switch compares a value against cases and runs the first match:

```chn
switch status {
    case 200:
        stdo("OK")
    case 404:
        stdo("Not Found")
    default:
        stdo("Unknown")
}
```

`break` exits a loop. `continue` skips to the next iteration.

### Functions

```chn
func add(a, b) {
    return a + b
}

stdo(add(3, 7))   -- 10
```

functions are first-class values. they can be stored in variables, passed as arguments, and returned:

```chn
func apply(f, value) {
    return f(value)
}

func triple(x) { return x * 3 }

stdo(apply(triple, 5))   -- 15
```

the compiler recognizes direct tail calls and eliminates them to avoid growing the call stack:

```chn
func sum_to(n, acc) {
    if n == 0 { return acc }
    return sum_to(n - 1, acc + n)   -- tail call, stack does not grow
}
```

functions can be nested. inner functions close over the outer scope.

### Lambdas and Closures

anonymous functions use `func` without a name, or the arrow shorthand for single-expression bodies:

```chn
let square = func(x) { return x * x }
let double = x -> x * 2
let add    = (a, b) -> a + b
```

lambdas capture variables by reference from the surrounding scope:

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
```

```chn
let nums   = [1, 2, 3, 4, 5, 6]
let evens  = nums.filter(x -> x % 2 == 0)
let result = evens.map(x -> x * 2)
stdo(result)   -- [4, 8, 12]
```

### Arrays

arrays are zero-indexed, ordered, resizable sequences. they hold values of any type including mixed types and nested arrays.

```chn
let fruits = ["apple", "banana", "cherry"]
stdo(fruits[0])          -- apple
stdo(fruits.length())    -- 3
fruits.push("date")
stdo(fruits.length())    -- 4
```

array methods:

| Method | Description |
|--------|-------------|
| `length()` | number of elements |
| `push(v)` / `add(v)` / `append(v)` | append to end |
| `pop()` | remove and return last element |
| `shift()` | remove and return first element |
| `insert(i, v)` | insert at index |
| `cut(i)` | remove element at index |
| `remove(v)` | remove first occurrence of value |
| `rall(v)` | remove all occurrences |
| `contains(v)` | true if value present |
| `index_of(v)` | index of first occurrence, -1 if absent |
| `sort()` | sort in place |
| `reverse()` | reverse in place |
| `copy()` | shallow copy |
| `fill(v)` | fill all slots with value |
| `join(sep)` | concatenate elements into a string |
| `map(f)` | new array with function applied to each element |
| `filter(f)` | new array with only elements where f returns true |
| `reduce(f, init)` | fold elements into a single value |
| `flat()` | flatten one level of nesting |
| `unique()` | deduplicated copy |
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

dicts are unordered string-keyed hash maps. created with `{}` literal syntax.

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

dict methods:

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

`struct` defines a named constructor function that creates a dict with named fields and default values. positional arguments fill fields in declaration order.

```chn
struct Vector3 {
    x: 0,
    y: 0,
    z: 0
}

let pos = Vector3(1, 2, 3)
stdo(pos.x)   -- 1
```

the result is a plain dict so all dict methods apply.

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

strings are immutable UTF-8 sequences. concatenation uses `+`. characters accessed with index notation.

```chn
let s = "Hello, CHN"
stdo(s[0])         -- H
stdo(s.length())   -- 10
stdo(s.upper())    -- HELLO, CHN
```

f-strings are prefixed with `f`. any expression wrapped in `{}` is evaluated and converted to a string at runtime.

```chn
let name = "world"
let n    = 42
stdo(f"Hello, {name}!")
stdo(f"The answer is {n}")
stdo(f"Double: {n * 2}")
stdo(f"Type: {typeof name}")
```

string methods:

| Method | Description |
|--------|-------------|
| `length()` | character count |
| `upper()` | uppercase copy |
| `lower()` | lowercase copy |
| `trim()` | strip leading and trailing whitespace |
| `split(sep)` | split on separator |
| `contains(sub)` | true if substring present |
| `starts_with(s)` | true if starts with s |
| `ends_with(s)` | true if ends with s |
| `replace(old, new)` | replace first occurrence |
| `find(sub)` | index of first occurrence, -1 if absent |
| `slice(start, len)` / `sub(start, len)` | return substring |
| `reverse()` | reversed copy |
| `to_num()` | parse as number |
| `pad_left(width, ch)` | left-pad to width |
| `pad_right(width, ch)` | right-pad to width |
| `repeat(n)` | concatenate string with itself n times |
| `char_at(i)` | single character at index |
| `count(sub)` | count non-overlapping occurrences |

### Error Handling

CHN uses `try`, `catch`, and `throw`. any value can be thrown. the caught value is bound to the variable in the catch clause.

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

throwing a dict gives structured error information:

```chn
throw { code: "INVALID_URL", message: f"bad url: {url}" }
```

try blocks can be nested. a throw inside a catch re-throws and is caught by the next outer handler.

### Imports and Modules

CHN resolves imports at compile time. three forms:

```chn
imp mathlib           -- bare name, runtime searches lib paths
imp ./helpers.chn     -- relative path
imp::lib mathlib      -- precompiled CCO bundle
```

module search order for bare names:

1. same directory as the importing file
2. `chn-libs/` walking up from the importing file toward the filesystem root
3. `chn-libs/` beside the CHN binary

functions must be marked `export` to be visible to importers:

```chn
export func clamp(x, lo, hi) {
    if x < lo { return lo }
    if x > hi { return hi }
    return x
}

func internal_helper(x) {   -- not exported, not visible outside this file
    return x * x
}
```

each file is loaded at most once per program run. circular imports are detected and blocked.

### Visibility

| Keyword | Access |
|---------|--------|
| `public` | callable from any file |
| `private` | only within the same source file |
| `protected` | within the same CCO bundle, not from outside |
| (none) | defaults to private |

`entry` designates the program entry point and must be combined with `public`:

```chn
public entry main() {
    stdo("starting")
}
```

visibility is enforced at compile time. calling a private function from another file is a compile error.

---

## Standard Library

### Output and Input

```chn
stdo(value)          -- print value followed by newline
stdi("prompt: ")     -- print without newline
let line = input()   -- read one line from stdin
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
os::hostname()              -- system hostname
os::pid()                   -- current process ID
os::system("ls -la")        -- run shell command, return exit code
os::chdir("/tmp")           -- change working directory
os::getcwd()                -- get current working directory
```

### File and Directory

```chn
file::read("path")             -- read entire file as string
file::write("path", text)      -- write string to file
file::append("path", text)     -- append string to file
file::exists("path")           -- true if file exists
file::delete("path")           -- delete file
file::size("path")             -- file size in bytes
file::lines("path")            -- read into array of lines
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
math::floor(x)          math::ceil(x)           math::round(x)
math::trunc(x)          math::abs(x)            math::sign(x)
math::sqrt(x)           math::pow(x, y)         math::log(x)
math::log(x, base)      math::log10(x)
math::sin(x)            math::cos(x)            math::tan(x)
math::atan2(y, x)
math::clamp(v, lo, hi)  math::lerp(a, b, t)
math::min(a, b)         math::max(a, b)
math::random()          -- float in [0, 1)
math::random(n)         -- float in [0, n)
math::pi()              math::e()
math::is_nan(x)         math::is_inf(x)
```

### Networking

```chn
net::tcp_listen(port)                     -- open TCP listener
net::tcp_accept(sock)                     -- accept connection
net::tcp_connect(host, port)              -- connect to host
net::send(sock, data)                     -- send string data
net::recv(sock, max_bytes)                -- receive data
net::close(sock)                          -- close socket
net::dns(hostname)                        -- resolve hostname
net::peer_addr(sock)                      -- get remote address
net::set_timeout(sock, ms)                -- set timeout

net::udp_bind(port)                       -- bind UDP socket
net::udp_send(sock, host, port, data)     -- send UDP datagram
net::udp_recv(sock, max_bytes)            -- receive UDP datagram

net::http_get(url)                        -- HTTP GET, return body
net::http_post(url, body)                 -- HTTP POST, return body

net::tls_listen(port)                     -- TLS listener
net::tls_accept(sock)                     -- accept TLS connection
net::tls_connect(host, port)              -- connect with TLS
net::tls_send(sock, data)                 -- send over TLS
net::tls_recv(sock, max_bytes)            -- receive over TLS
net::tls_close(sock)                      -- close TLS connection
```

TLS functions require OpenSSL at build time. on systems without TLS, these return a runtime error.

### Binary I/O

```chn
bin::write("path", array_of_bytes)   -- write byte array to file
bin::read("path")                    -- read file as array of byte values
bin::write_num("path", n)            -- write number as raw bytes
```

### Utility

```chn
str(value)               -- convert any value to string
len(value)               -- length of array, string, or dict
range(stop)
range(start, stop)
range(start, stop, step)
```

---

## Bytecode and CCO Bundles

CHN has three compiled artifact formats.

**`.chn2` program bytecode** -- a single compiled program containing the entry chunk, embedded function table, and import records. produced with `-c`. can be executed directly.

**`.function` function bundle** -- contains only exported functions, no entry point. produced with `-c --func`. useful for distributing a small set of utilities.

**`.cco` CHN Compiled Object** -- packs one or more compiled source units into a single binary container. if any unit has `public entry main()`, the CCO can be run directly. otherwise it is a library imported with `imp::lib`. CCO files embed their own import dependency list so the runtime can resolve dependencies automatically from a flat directory.

```sh
chn mathlib.chn -oc mathlib.cco        -- build library CCO
chn app.chn -oc app.cco                -- build runnable CCO
chn app.cco                            -- run it
chn mathlib.chn --no-minify -oc mathlib.cco    -- human-readable bytecode for debugging
```

---

## The REPL

running `chn` with no arguments starts the REPL. expressions and statements execute immediately. state persists between lines within the same session. history is saved to `~/.chn_history` (up to 500 entries).

```
CHN 1.1  Interactive REPL
Type :help for commands. :q or Ctrl-D to exit.

chn> let x = 10
chn> stdo(x * x)
100
chn> :q
Bye!
```

multi-line input: leave a `{`, `(`, or `[` open and press Enter. the prompt changes to `... N>` where N is the open bracket depth. close the brackets to execute.

REPL commands:

| Command | Effect |
|---------|--------|
| `:q` or `exit` or `quit` | exit |
| `:help` | show commands |
| `:gc` | GC statistics |
| `:mem` | full memory report |
| `:vars` | global variable slot count |
| `:clear` | clear the screen |
| `:reset` | reset VM state and GC |
| `:time` | toggle execution timing |
| `:load <file>` | execute a .chn file in the current REPL context |
| `:ast <expr>` | print the AST for an expression |
| `:dis <expr>` | disassemble an expression to bytecode |

if readline is available at build time, the REPL uses it for line editing and history navigation. otherwise it falls back to `fgets`.

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

flags:
  -O0 / -O1 / -O2      optimization level (default -O2)
  --check  / -k        syntax check only, no execution
  --disasm / -d        disassemble bytecode to stdout
  --ast    / -a        dump AST to stdout
  --func   / -f        compile to function bundle
  --no-minify          disable bytecode minification
  --version / -v       print version string
  --help    / -h       print usage
```

use `--` to force everything after it into `os::args()`:

```sh
chn script.chn -- --version --help
```

---

## Running Tests

the test suite is at `docs/tests/test_all.sh`. it requires bash and a built `chn` binary. tests cover arithmetic, strings, control flow, functions, closures, arrays, dicts, structs, error handling, switch, runtime arguments, CCO compilation, multi-level CCO import chains, and bytecode minification.

```sh
bash docs/tests/test_all.sh           -- uses chn from PATH
bash docs/tests/test_all.sh ./chn     -- uses a specific binary
```

the script prints PASS or FAIL for each test case. on failure it shows expected and actual output. exits 0 if all pass, 1 if any fail.

---

## Architecture

the implementation lives in `src/v1.1/` (C99 core) and `src/memory/` (C++ wrapper layer).

| Module | Role |
|--------|------|
| `lexer.c` / `lexer.h` | tokenizes source text |
| `parser.c` / `parser.h` | recursive descent parser, produces AST |
| `ast.c` / `ast.h` | AST node types and printer |
| `compiler.c` / `compiler.h` | single-pass compiler from AST to bytecode chunks |
| `vm.c` / `vm.h` | stack-based VM that executes bytecode |
| `gc.c` / `gc.h` | generational GC with slab allocator and intern table |
| `func.c` / `func.h` | function object and global function registry |
| `bytecode.c` / `bytecode.h` | serialization for `.chn2`, `.function`, `.cco` |
| `native.c` / `native.h` | OS, file, math, binary, and utility dispatch |
| `native_net.c` | TCP, UDP, TLS, HTTP native functions |
| `error.c` / `error.h` | source-location-aware error reporting |
| `common.h` | value model, opcode table, method dispatch, constants |
| `main.c` | CLI parsing, import resolution, program entry |
| `src/memory/MemoryHandler.cpp` | GC stats and memory reporting |
| `src/memory/RAII.cpp` | RAII guards: MallocGuard, ChunkGuard, VMGuard, defer |
| `src/memory/Repl.cpp` | interactive REPL |
| `src/memory/MemoryTypes.cpp` | shared type definitions for the C++ layer |

**VM internals** -- the VM maintains a flat value stack and a separate call frame stack. each call frame stores the executing function, current instruction pointer, and base index into the value stack for that frame's locals. a parallel try-frame stack tracks active error handlers.

**GC internals** -- newly allocated objects enter the young generation. objects that survive a configurable number of minor collections are promoted to the old generation. short strings under 128 bytes are carved from 64 KB slab arenas. a weak-reference intern table deduplicates short strings so identical values share one allocation. write barriers keep inter-generational references consistent during minor collections.

**bytecode format** -- section-based layout. each file begins with a magic number and version field, followed by length-prefixed sections for imports, exports, function bodies, the entry chunk, debug symbols, and bundle-internal functions. the CCO container wraps multiple compiled units with a header recording unit count, which unit has the entry point, and flags.

**compiler** -- single-pass from AST to bytecode. constant folding, dead-code elimination, and tail-call detection are performed at compile time under `-O2`. the compiler tracks open scopes, resolves local variables by slot index, and patches forward jump offsets in a second pass over the generated instruction stream.

---

## Runtime Limits

these constants are defined in `common.h` and can be changed before recompiling.

| Limit | Value |
|-------|-------|
| max identifier length | 256 characters |
| max string literal length | 65536 characters |
| max function parameters | 64 |
| max call stack depth | 512 frames |
| max value stack size | 8192 values |
| max global variables | 1024 |
| max locals per function | 512 |
| max functions per compiler unit | 512 |
| max try handler nesting depth | 64 |
| max break/continue patch sites | 256 |
| max imported files per program | 256 |
| max source files per CCO bundle | 64 |

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
