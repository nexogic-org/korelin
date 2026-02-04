//
// Created by Helix on 2026/1/10.
//

#include "kgc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define GC_HEAP_GROW_FACTOR 2
#define GC_INITIAL_THRESHOLD (1024 * 1024) // 1MB

// --- 內部輔助函數聲明 ---
static void mark_roots(KGC* gc);
static void sweep(KGC* gc);
static void blacken_object(KGC* gc, KObjHeader* obj);

// --- API 實現 ---

void kgc_init(KGC* gc, KVM* vm) {
    gc->head = NULL;
    gc->bytes_allocated = 0;
    gc->next_gc_threshold = GC_INITIAL_THRESHOLD;
    gc->vm = vm;
    gc->gc_count = 0;
}

void kgc_free(KGC* gc) {
    // 釋放所有對象，不進行標記，直接清空
    KObjHeader* obj = gc->head;
    while (obj != NULL) {
        KObjHeader* next = obj->next;
        // free(obj); // 對象頭和數據是一起分配的，直接釋放頭指針即可
        // 如果對象內部持有非 GC 管理的資源 (如 FILE*), 需要先調用析構
        free(obj);
        obj = next;
    }
    gc->head = NULL;
    gc->bytes_allocated = 0;
}

void* kgc_alloc(KGC* gc, size_t size, KObjType type) {
    // 觸發策略：如果當前分配量超過閾值，則觸發 GC
    if (gc->bytes_allocated > gc->next_gc_threshold) {
        kgc_collect(gc);
    }

    // 計算總大小：頭部 + 數據
    // 注意內存對齊 (這裏簡化處理，malloc 通常保證指針對齊)
    size_t total_size = sizeof(KObjHeader) + size;
    
    KObjHeader* header = (KObjHeader*)malloc(total_size);
    if (header == NULL) {
        // 嘗試緊急 GC
        kgc_collect(gc);
        header = (KObjHeader*)malloc(total_size);
        if (header == NULL) {
            fprintf(stderr, "[KGC] Out of memory! Failed to allocate %zu bytes.\n", total_size);
            exit(1);
        }
    }

    // 初始化頭部
    header->type = type;
    header->marked = false;
    header->size = total_size;
    
    // 插入到鏈表頭部 (O(1))
    header->next = gc->head;
    gc->head = header;

    gc->bytes_allocated += total_size;

    // 清零數據區 (安全起見)
    void* data_ptr = (void*)(header + 1);
    memset(data_ptr, 0, size);

#ifdef DEBUG_GC
    printf("[KGC] Alloc type %d, size %zu, total %zu at %p\n", type, size, total_size, data_ptr);
#endif

    return data_ptr;
}

void kgc_collect(KGC* gc) {
#ifdef DEBUG_GC
    printf("[KGC] --- GC Begin ---\n");
    size_t before = gc->bytes_allocated;
#endif

    if (gc->vm == NULL) {
        // 如果沒有 VM 引用，無法標記根，為了安全不回收 (或者全回收?)
        // 這裏假設初始化正確，不應發生
        return;
    }

    mark_roots(gc);
    // 注意：如果有灰色集合 (工作隊列)，這裏需要處理直到隊列為空
    // 目前實現是遞歸的 mark_obj，相當於隱式棧
    
    sweep(gc);

    // 更新閾值
    gc->next_gc_threshold = gc->bytes_allocated * GC_HEAP_GROW_FACTOR;
    gc->gc_count++;

#ifdef DEBUG_GC
    printf("[KGC] --- GC End --- Freed %zu bytes, Now %zu bytes\n", before - gc->bytes_allocated, gc->bytes_allocated);
#endif
}

KObjHeader* kgc_get_header(void* ptr) {
    return ((KObjHeader*)ptr) - 1;
}

// --- 標記階段 (Mark) ---

void kgc_mark_obj(KGC* gc, KObjHeader* obj) {
    if (obj == NULL || obj->marked) return;
    
#ifdef DEBUG_GC
    printf("[KGC] Mark object at %p (type %d)\n", obj, obj->type);
#endif

    obj->marked = true;
    
    // 黑化對象：標記該對象引用的其他對象
    blacken_object(gc, obj);
}

void kgc_mark_value(KGC* gc, KValue value) {
    if (value.type == VAL_OBJ || value.type == VAL_STRING) {
        if (value.as.obj == NULL) return;
        KObjHeader* header = kgc_get_header(value.as.obj);
        kgc_mark_obj(gc, header);
    }
}

static void mark_roots(KGC* gc) {
    KVM* vm = gc->vm;
    
    // 1. 標記棧上的值
    for (KValue* slot = vm->stack; slot < vm->stack_top; slot++) {
        kgc_mark_value(gc, *slot);
    }

    // 2. 標記寄存器中的值
    for (int i = 0; i < KVM_REGISTERS_MAX; i++) {
        kgc_mark_value(gc, vm->registers[i]);
    }

    // 3. 標記調用幀中的引用 (如果有的話，例如閉包)
    // 目前 CallFrame 裏只有 chunk 和 ip，沒有堆引用，除非未來加入閉包
    
    // 4. 標記全局變量 (如果有的話)
    // 假設全局變量存儲在某個全局 Table 中，該 Table 應該被標記
}

static void blacken_object(KGC* gc, KObjHeader* obj) {
    // 根據對象類型，遍歷其內部引用的對象
    switch (obj->type) {
        case OBJ_STRING:
            // 字符串不引用其他對象
            break;
        case OBJ_CLASS_INSTANCE:
            // TODO: 遍歷實例的字段 (Fields)
            // KInstance* instance = (KInstance*)(obj + 1);
            // for (int i=0; i < instance->field_count; i++) kgc_mark_value(gc, instance->fields[i]);
            break;
        case OBJ_ARRAY:
            // TODO: 遍歷數組元素
            break;
        default:
            break;
    }
}

// --- 清除階段 (Sweep) ---

static void sweep(KGC* gc) {
    KObjHeader* prev = NULL;
    KObjHeader* obj = gc->head;
    
    while (obj != NULL) {
        if (obj->marked) {
            // 對象存活，重置標記位，繼續下一個
            obj->marked = false;
            prev = obj;
            obj = obj->next;
        } else {
            // 對象未被標記，回收
            KObjHeader* unreached = obj;
            obj = obj->next;
            
            if (prev != NULL) {
                prev->next = obj;
            } else {
                gc->head = obj;
            }

#ifdef DEBUG_GC
            printf("[KGC] Freeing object at %p (type %d, size %zu)\n", unreached, unreached->type, unreached->size);
#endif

            gc->bytes_allocated -= unreached->size;
            free(unreached);
        }
    }
}
