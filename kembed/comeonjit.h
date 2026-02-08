#ifndef KORELIN_COMEONJIT_H
#define KORELIN_COMEONJIT_H

#define COMEON_JIT_VERSION "1.0.0"

#include "kcode.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief JIT 編譯器接口
 * "ComeOnJIT" - A simple Just-In-Time compiler for Korelin
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
 * @brief JIT 編譯器結構體
 */
typedef struct {
    bool enabled;
    JitArch arch;
    
    /** 可執行內存區域 */
    uint8_t* exec_memory;
    size_t exec_memory_size;
    size_t exec_memory_used;
    
    /** 統計 */
    size_t compiled_functions;
} ComeOnJIT;

/** @brief API */

void jit_init(ComeOnJIT* jit);
void jit_cleanup(ComeOnJIT* jit);

/**
 * @brief 嘗試將字節碼塊編譯為本地機器碼
 * 返回指向機器碼的函數指針 (如果成功)，否則返回 NULL
 * 函數簽名通常為：int (*)(KVM* vm, KValue* args)
 */
typedef int (*JitFunction)(void* vm);

void* jit_compile(ComeOnJIT* jit, KBytecodeChunk* chunk);

/** @brief 內部輔助 (IR & Codegen) */

/**
 * @brief 簡單的中間表示指令 (如果需要多 pass 優化)
 * 目前我們採用單遍編譯 (One-pass compilation) 策略：Bytecode -> Machine Code
 */

/** @brief 內存管理 */
uint8_t* jit_alloc_exec(ComeOnJIT* jit, size_t size);

#endif /** KORELIN_COMEONJIT_H */
