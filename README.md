# Nexogic Korelin

<div align="center">
  <img src="doc/logo.png" alt="Korelin Logo" width="128" height="128" />
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

## 5. Documentation Navigation

*   **Basic Syntax**
    *   [Variables and Constants](doc/变量与常量.md)
    *   [Keywords](doc/关键字.md)
    *   [Conditions](doc/条件.md)
    *   [Loops](doc/循环.md)
    *   [Functions and Main](doc/函数与主函数.md)
    *   [Type Conversion](doc/类型转换.md)
*   **Data Structures and Generics**
    *   [Arrays](doc/数组.md)
    *   [Generics](doc/泛型.md)
    *   [Maps](doc/Map.md)
    *   [Structs](doc/结构体.md)
*   **Object-Oriented Programming**
    *   [OOP](doc/面向对象编程.md)
    *   [Inheritance](doc/类的继承.md)
*   **Error Handling**
    *   [Exceptions and Handling](doc/异常与异常处理.md)
*   **Standard Library and Tools**
    *   [Standard Library](doc/标准库.md)
    *   [Command Line Tools](doc/命令行.md)
    *   [Package Management](doc/包管理.md)
*   **Internals and Extensions**
    *   [KVM Internals](doc/KVM原理.md)
    *   [Bytecode Specification](doc/字节码.md)
    *   [C/C++ API Guide](doc/c-c++API.md)

## License

MIT License
