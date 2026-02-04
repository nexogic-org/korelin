# Korelin Programming Language

Korelin is a statically typed, object-oriented programming language designed for performance and simplicity. It features a Just-In-Time (JIT) compiler and a lightweight virtual machine.

## Features

*   **Static Typing**: Ensures type safety at compile time.
*   **Object-Oriented**: Classes, inheritance, and encapsulation.
*   **JIT Compilation**: Compiles bytecode to native machine code for high performance.
*   **Lightweight**: Minimal dependencies and fast startup.

## Build

Requirements:
*   CMake 3.10+
*   C Compiler (GCC/Clang/MSVC)

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Usage

```bash
./bin/korelin run path/to/script.k
```

## License

MIT License
