# Nexogic Korelin

<div align="center">
  <a href="https://korelin.org"><img src="logo.png" alt="Korelin Logo" width="128" height="128" /></a>
  <br/>
  <h3>Nexogic Korelin Programming Language</h3>
</div>

## 1. What is Korelin?

Korelin (/ˈkɒrəlɪn/) is a statically typed, object-oriented programming language running on the KVM (Korelin Virtual Machine). It aims to provide concise syntax, a powerful type system, and efficient integration capabilities with C/C++ applications.

The design philosophy of Korelin is **"Concise, Comprehensive, High Performance"**. It is suitable both as a standalone application development language and as an embedded scripting language for game engines or large-scale software.

## 2. Core Features

*   **Static Type System**: Provides compile-time type checking to reduce runtime errors and supports type inference.
*   **Object-Oriented**: Supports OOP features such as classes, inheritance, polymorphism, and encapsulation.
*   **High-Performance KVM**: Register-based virtual machine architecture with compact instructions and high execution efficiency.
*   **JIT Compilation Optimization**: Uses ComeOn JIT (proprietary development) for just-in-time compilation optimization, significantly improving execution efficiency.
*   **C/C++ Interoperability**: Natively supports bidirectional calls with C/C++, making it easy to extend and embed.
*   **Modern Syntax**: Combines the advantages of languages like C, Java, and Go, with a smooth learning curve.
*   **Package Management**: Built-in `rungo` package manager for convenient dependency management and project builds.

## 3. Quick Start

### Write Code

Create a `main.kri` file:

```korelin
// main.kri
import os;

int main() {
    os.print("Hello, Korelin!");
    return 0;
}
```

### Run

Run using the command line:

```bash
korelin run main.kri
```

## 4. Build Guide

### Requirements
*   CMake 3.10+
*   C Compiler (GCC/Clang/MSVC)

### Build Steps

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## License

MIT License
