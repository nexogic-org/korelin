//
// Created by Helix on 2026/1/10.
//

#ifndef KORELIN_KGC_H
#define KORELIN_KGC_H

#include <stddef.h>
#include <stdbool.h>
#include "kvm.h" // 需要訪問 VM 狀態以標記根對象

// KObjType and KObjHeader are defined in kvm.h to avoid circular dependency

// GC 狀態
typedef struct KGC {
    KObjHeader* head;         // 所有分配對象的鏈表頭
    size_t bytes_allocated;   // 當前已分配的總字節數
    size_t next_gc_threshold; // 觸發下一次 GC 的閾值
    KVM* vm;                  // 反向引用 VM 以獲取根 (Roots)
    
#ifdef _WIN32
    void* heap_handle;        // Windows 私有堆句柄 (HANDLE)
#endif

    // 統計信息
    size_t gc_count;          // GC 觸發次數
} KGC;

// --- API ---

// 初始化 GC
void kgc_init(KGC* gc, KVM* vm);

// 銷毀 GC (釋放所有對象)
void kgc_free(KGC* gc);

// 分配內存並註冊到 GC
// 這是 GC 對外的核心接口，替代 malloc
void* kgc_alloc(KGC* gc, size_t size, KObjType type);

// 顯式觸發垃圾回收
void kgc_collect(KGC* gc);

// 輔助：標記對象 (通常由 VM 在 stack/roots 掃描時調用)
void kgc_mark_obj(KGC* gc, KObjHeader* obj);
void kgc_mark_value(KGC* gc, KValue value);

// 輔助：從數據指針獲取頭部
KObjHeader* kgc_get_header(void* ptr);

#endif //KORELIN_KGC_H
