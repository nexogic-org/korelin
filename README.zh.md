# Korelin 编程语言

Korelin 是一门为性能和简洁性设计的静态类型、面向对象编程语言。它内置了即时编译器 (JIT) 和轻量级虚拟机。

## 特性

*   **静态类型**: 在编译时确保类型安全。
*   **面向对象**: 支持类、继承和封装。
*   **JIT 编译**: 将字节码编译为原生机器码以获得高性能。
*   **轻量级**: 依赖少，启动快。

## 构建

系统要求:
*   CMake 3.10+
*   C 编译器 (GCC/Clang/MSVC)

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## 使用

```bash
./bin/korelin run path/to/script.k
```

## 许可证

MIT License
