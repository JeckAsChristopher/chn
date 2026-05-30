# CHN 1.1 — Release Notes

## Overview

CHN 1.1 is a maintenance and enhancement release building on the 1.0 foundation.
It adds an expanded standard library, a security fix for the CCO bytecode format,
GC tuning, and several bug fixes.

---

## What's New

### Security Fix — CCO v4 Adler-32 Payload Checksum

**Previous behaviour (v1–v3):** CCO bundles had a header-only checksum. Payload
bytes could be altered without detection; the runtime would silently load
tampered bytecode.

**v1.1 fix:** CCO format version bumped to **4**. A 32-bit Adler-32 checksum of
the entire payload (all bytes after the 16-byte header) is stored in the header.
`cco_open()` verifies the checksum on load and rejects any file that fails with:

```
error: load '<file>': checksum mismatch (file may be corrupted)
```

Old v1/v2/v3 files remain loadable (backward-compatible reads); they are simply
not checksum-verified. All newly written CCO files use the v4 format.

---

### Extended Standard Library

All new functions are accessed via `__native__(id, ...)`. IDs are listed in
`src/v1.1/native.h`.

#### Extended Math (`0x0217`–`0x0226`)

| Function | ID | Description |
|---|---|---|
| `asin(x)` | `0x0217` | Inverse sine (radians) |
| `acos(x)` | `0x0218` | Inverse cosine |
| `atan(x)` | `0x0219` | Inverse tangent |
| `sinh(x)` | `0x021A` | Hyperbolic sine |
| `cosh(x)` | `0x021B` | Hyperbolic cosine |
| `tanh(x)` | `0x021C` | Hyperbolic tangent |
| `exp(x)` | `0x021D` | e^x |
| `exp2(x)` | `0x021E` | 2^x |
| `log2(x)` | `0x021F` | Base-2 logarithm |
| `cbrt(x)` | `0x0220` | Cube root |
| `hypot(a,b)` | `0x0221` | √(a²+b²) |
| `gcd(a,b)` | `0x0222` | Greatest common divisor |
| `lcm(a,b)` | `0x0223` | Least common multiple |
| `factorial(n)` | `0x0224` | n! (integer, up to ~20) |
| `deg(r)` | `0x0225` | Radians → degrees |
| `rad(d)` | `0x0226` | Degrees → radians |

#### Type Coercion (`0x0403`–`0x0405`)

| Function | ID | Description |
|---|---|---|
| `int(val)` | `0x0403` | Truncate to integer number |
| `float(val)` | `0x0404` | Convert to floating-point |
| `bool(val)` | `0x0405` | Truthy/falsy → boolean |

#### String Utilities (`0x0406`–`0x040A`)

| Function | ID | Description |
|---|---|---|
| `format(fmt,...)` | `0x0406` | printf-style string formatting |
| `bytes(str)` | `0x0407` | String → array of byte integers |
| `from_bytes(arr)` | `0x0408` | Byte array → string |
| `ord(ch)` | `0x0409` | Single char → Unicode codepoint |
| `chr(n)` | `0x040A` | Unicode codepoint → char string |

#### OS Extras

| Function | ID | Description |
|---|---|---|
| `popen(cmd)` | `0x000E` | Run shell command, capture stdout |
| `cpu_count()` | `0x000F` | Number of logical CPU cores |

#### JSON (`0x0600`–`0x0601`)

| Function | ID | Description |
|---|---|---|
| `json_parse(str)` | `0x0600` | Parse JSON string → CHN value |
| `json_stringify(val)` | `0x0601` | CHN value → JSON string |

---

### GC Tuning

| Parameter | v1.0 | v1.1 |
|---|---|---|
| `INTERN_CAP` (string intern table) | 8 192 | 32 768 |
| `GC_YOUNG_THRESHOLD` | 512 KB | 1 MB |
| `GC_MAJOR_THRESHOLD` | 8 MB | 16 MB |
| `GC_STEP_SIZE` | 256 | 512 |
| `GC_MAJOR_EVERY` | 8 cycles | 12 cycles |

These changes reduce GC frequency for workloads that create many short-lived
strings and reduce full-heap pause frequency on larger programs.

---

### Bug Fixes

- **Dict key comparison in GC** (`gc.c`): `gc_dict_get` and `gc_dict_delete`
  previously compared keys by pointer identity only. When the intern table was
  full, two equal strings could hash to different pointers, silently returning
  wrong results. Fixed by adding a `KEYS_EQUAL` macro that falls back to
  content comparison (hash + length + `memcmp`) when interning fails.

- **str() nested containers** (`native.c`): `str([arr, dict])` previously
  showed `[..N..]` / `{..N..}` placeholders for nested arrays and dicts.
  v1.1 fully recurses up to 8 levels deep, so `str([[1,2],[3,4]])` now
  correctly renders `[[1, 2], [3, 4]]`.

- **File read limit** (`native.c`): `file::read` raised from 8 MB to 64 MB.

---

### Building v1.1

```sh
# Linux / macOS / Android (Termux)
clang -O2 -std=c99 -o chn src/v1.1/*.c -lssl -lcrypto -lm -lpthread -ldl

# If OpenSSL headers are in a non-standard path:
clang -O2 -std=c99 -I/path/to/openssl/include -L/path/to/openssl/lib \
      -o chn src/v1.1/*.c -lssl -lcrypto -lm -lpthread -ldl

# CMake (update CMakeLists.txt src path to v1.1):
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

---

## Compatibility

- All CHN 1.0 source files are 100% compatible with CHN 1.1.
- CCO bundles compiled with CHN 1.0 (v1–v3 format) are still loadable.
- CCO bundles compiled with CHN 1.1 use v4 format and cannot be loaded by
  CHN 1.0 (it will report an unsupported version error).
