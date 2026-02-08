# Nexogic Korelin

<div align="center">
  <a href="https://korelin.org"><img src="logo.png" alt="Korelin Logo" width="128" height="128" /></a>
  <br/>
  <h3>Nexogic Korelin Programming Language</h3>
</div>

## 1. 什麼是 Korelin？

Korelin (/ˈkɒrəlɪn/) 是一門運行在 KVM (Korelin Virtual Machine) 上的靜態類型、面向對象編程語言。它旨在提供簡潔的語法、強大的類型系統以及與 C/C++ 應用程序的高效集成能力。

Korelin 的設計理念是**「簡潔、功能全面、高性能」**。它既適合作為獨立的應用開發語言，也適合作為遊戲引擎或大型軟件的嵌入式腳本語言。

## 2. 核心特性

*   **靜態類型系統**：提供編譯時類型檢查，減少運行時錯誤，支持類型推斷。
*   **面向對象**：支持類、繼承、多態、封裝等 OOP 特性。
*   **高性能 KVM**：基於寄存器的虛擬機架構，指令緊湊，執行效率高。
*   **JIT 編譯優化**：使用了 ComeOn JIT（獨家開發）進行即時編譯優化，顯著提升執行效率。
*   **C/C++ 互操作**：原生支持與 C/C++ 的雙向調用，易於擴展和嵌入。
*   **現代語法**：融合了 C、Java、Go 等語言的優點，學習曲線平緩。
*   **包管理**：內置 `rungo` 包管理器，方便依賴管理和項目構建。

## 3. 快速開始

### 編寫代碼

創建一個 `main.kri` 文件：

```korelin
// main.kri
import os;

int main() {
    os.print("Hello, Korelin!");
    return 0;
}
```

### 運行

使用命令行運行：

```bash
korelin run main.kri
```

## 4. 構建指南

### 系統要求
*   CMake 3.10+
*   C 編譯器 (GCC/Clang/MSVC)

### 編譯步驟

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## 版本 1.0.4 更新

*   **多參數支持**：`dynlib.call` 現已支持傳遞多個參數（最多 16 個），解決了之前版本只能傳遞 1 個參數的問題。
*   **代碼優化**：統一了代碼註釋風格為繁體中文 Javadoc 格式。
*   **修復缺陷**：
    *   **線程隔離**：修復了子線程無法訪問主線程全局變量的問題，現在通過快照機制共享全局變量（只讀副本）。
    *   **模塊導入**：修復了特定路徑下導入模塊導致的崩潰問題，增加了路徑長度檢查和對象驗證。
    *   **邏輯運算**：修復了 `&&`、`||`、`!` 運算符不支持布爾值的問題，並修復了相關的類型錯誤崩潰。
    *   **變量作用域**：修復了 `if`/`while` 塊中變量聲明提升（Hoist）導致的污染外部變量問題。

## 許可證

MIT License