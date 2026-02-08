#ifndef KORELIN_COMEONJIT_H
#define KORELIN_COMEONJIT_H

#include "kcode.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief JIT 編譯器接口
 * Korelin 的簡易即時編譯器 (ComeOnJIT)
 */

/**
 * @brief 支持的目標架構
 */
typedef enum {
    JIT_ARCH_X64,
    JIT_ARCH_ARM64, /**< 預留 */
    JIT_ARCH_UNKNOWN
} JitArch;

/**
 * @brief JIT 編譯器狀態結構
 */
typedef struct ComeOnJIT {
    bool enabled;
    JitArch arch;
    
    /* 可執行內存區域 */
    uint8_t* exec_memory;      /**< 可執行內存指針 */
    size_t exec_memory_size;   /**< 分配的內存大小 */
    size_t exec_memory_used;   /**< 已使用的內存大小 */
    
    /* 統計 */
    size_t compiled_functions; /**< 已編譯函數數量 */
} ComeOnJIT;

// --- API ---

/**
 * @brief 初始化 JIT 編譯器
 */
void jit_init(ComeOnJIT* jit);

/**
 * @brief 銷毀 JIT 編譯器
 */
void jit_cleanup(ComeOnJIT* jit);

/**
 * @brief JIT 編譯函數類型定義
 * 嘗試將字節碼塊編譯為本地機器碼
 * 返回指向機器碼的函數指針 (如果成功)，否則返回 NULL
 * 函數簽名通常為：int (*)(KVM* vm, KValue* args)
 */
typedef int (*JitFunction)(void* vm);

/**
 * @brief 編譯字節碼塊
 * @param jit JIT 實例
 * @param chunk 字節碼塊
 * @return 編譯後的函數指針，失敗返回 NULL
 */
void* jit_compile(ComeOnJIT* jit, KBytecodeChunk* chunk);

// --- 內部輔助 (IR & Codegen) ---

/**
 * @brief 簡單的中間表示指令 (如果需要多 pass 優化)
 * 目前我們採用單遍編譯 (One-pass compilation) 策略：Bytecode -> Machine Code
 */

/**
 * @brief 分配可執行內存
 */
uint8_t* jit_alloc_exec(ComeOnJIT* jit, size_t size);

#endif //KORELIN_COMEONJIT_H
