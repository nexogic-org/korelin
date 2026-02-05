# Nexogic Korelin

<div align="center">
  <a href="https://korelin.org"><img src="logo.png" alt="Korelin Logo" width="128" height="128" /></a>
  <br/>
  <h3>Nexogic Korelin Programming Language</h3>
</div>

## 1. 什么是 Korelin？

Korelin (/ˈkɒrəlɪn/) 是一门运行在 KVM (Korelin Virtual Machine) 上的静态类型、面向对象编程语言。它旨在提供简洁的语法、强大的类型系统以及与 C/C++ 应用程序的高效集成能力。

Korelin 的设计理念是**“简洁、功能全面、高性能”**。它既适合作为独立的应用开发语言，也适合作为游戏引擎或大型软件的嵌入式脚本语言。

## 2. 核心特性

*   **静态类型系统**：提供编译时类型检查，减少运行时错误，支持类型推断。
*   **面向对象**：支持类、继承、多态、封装等 OOP 特性。
*   **高性能 KVM**：基于寄存器的虚拟机架构，指令紧凑，执行效率高。
*   **JIT 编译优化**：使用了 ComeOn JIT（独家开发）进行即时编译优化，显著提升执行效率。
*   **C/C++ 互操作**：原生支持与 C/C++ 的双向调用，易于扩展和嵌入。
*   **现代语法**：融合了 C、Java、Go 等语言的优点，学习曲线平缓。
*   **包管理**：内置 `rungo` 包管理器，方便依赖管理和项目构建。

## 3. 快速开始

### 编写代码

创建一个 `main.kri` 文件：

```korelin
// main.kri
import os;

int main() {
    os.print("Hello, Korelin!");
    return 0;
}
```

### 运行

使用命令行运行：

```bash
korelin run main.kri
```

## 4. 构建指南

### 系统要求
*   CMake 3.10+
*   C 编译器 (GCC/Clang/MSVC)

### 编译步骤

```bash
mkdir build
cd build
cmake ..
cmake --build .
```


## 许可证

MIT License
