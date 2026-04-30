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

CHN is a statically structured, dynamically typed scripting language implemented in C. It compiles source code to a compact bytecode format and runs it on a stack based virtual machine. The runtime is self contained with no external dependencies. CHN is designed to be lightweight and portable, targeting POSIX systems including constrained environments like Android and Termux on AArch64 hardware.

Source files are plain text with a `.chn` extension. Programs can be run directly from source, compiled to portable bytecode, or bundled into distributable CCO packages.

Licensed under the Apache License 2.0. Copyright 2025 Jeck Christopher Anog.

---

## Table of Contents

- [Features](#features)
- [Building](#building)
- [Usage](#usage)
- [Language Overview](#language-overview)
  - [Entry Point](#entry-point)
  - [Variables and Types](#variables-and-types)
  - [Operators](#operators)
  - [Control Flow](#control-flow)
  - [Functions](#functions)
  - [Lambdas](#lambdas)
  - [Arrays](#arrays)
  - [Dicts](#dicts)
  - [Structs](#structs)
  - [Strings](#strings)
  - [Error Handling](#error-handling)
  - [Imports and Modules](#imports-and-modules)
  - [Visibility](#visibility)
  - [Native Functions](#native-functions)
- [Bytecode and CCO Bundles](#bytecode-and-cco-bundles)
- [CLI Reference](#cli-reference)
- [Running Tests](#running-tests)
- [Architecture](#architecture)
- [Limits](#limits)

---

## Features

- Compiled to compact bytecode with an optimizing single pass compiler
- Register style stack VM with tail call optimization
- Generational garbage collector with slab allocation for short strings
- Three tier module system: source import, compiled bytecode, and CCO bundles
- First class functions and closures
- f-string interpolation with arbitrary expressions
- try / catch / throw error handling
- Comprehensive standard library covering OS, file I/O, math, networking, and binary I/O
- Interactive REPL with AST and disassembly debug modes
- Three optimization levels via -O0, -O1, and -O2
- Optional bytecode minification for smaller output files
- Runtime argument passing via os::args()

---

## Building

CHN is written in C99 with POSIX extensions. Compile all `.c` files in `src/v1.0/` together:

```sh
gcc -O2 -o chn src/v1.0/*.c -lm
```

On Android or Termux:

```sh
clang -O2 -o chn src/v1.0/*.c -lm
```

The resulting binary is the complete CHN interpreter, compiler, and REPL. No
