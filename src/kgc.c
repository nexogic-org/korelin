//
// Created by Helix on 2026/1/10.
//

#include "kgc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

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
#ifdef _WIN32
    // Create a private heap. 
    // Options: 0 (defaults), InitialSize=0, MaximumSize=0 (growable)
    gc->heap_handle = HeapCreate(0, 0, 0);
    if (gc->heap_handle == NULL) {
        fprintf(stderr, "[KGC] Failed to create Windows Heap! Error: %lu\n", GetLastError());
        exit(1);
    }
#endif
}

void kgc_free(KGC* gc) {
    // 釋放所有對象，不進行標記，直接清空
    KObjHeader* obj = gc->head;
    while (obj != NULL) {
        KObjHeader* next = obj->next;
        // free(obj); // 對象頭和數據是一起分配的，直接釋放頭指針即可
        // 如果對象內部持有非 GC 管理的資源 (如 FILE*), 需要先調用析構
        
        // Free internal resources (CRT Heap)
        switch (obj->type) {
            case OBJ_STRING: {
                KObjString* str = (KObjString*)obj;
                if (str->chars) free(str->chars);
                break;
            }
            case OBJ_ARRAY: {
                KObjArray* arr = (KObjArray*)obj;
                if (arr->elements) free(arr->elements);
                break;
            }
            case OBJ_CLASS_INSTANCE: {
                KObjInstance* ins = (KObjInstance*)obj;
                free_table(&ins->fields);
                break;
            }
            case OBJ_CLASS: {
                KObjClass* cls = (KObjClass*)obj;
                free_table(&cls->methods);
                if (cls->name) free(cls->name);
                break;
            }
            case OBJ_FUNCTION: {
                KObjFunction* func = (KObjFunction*)obj;
                if (func->name) free(func->name);
                break;
            }
            case OBJ_NATIVE: {
                KObjNative* nat = (KObjNative*)obj;
                if (nat->name) free(nat->name);
                break;
            }
            default: break;
        }

#ifdef _WIN32
        HeapFree(gc->heap_handle, 0, obj);
#else
        free(obj);
#endif
        obj = next;
    }
    gc->head = NULL;
    gc->bytes_allocated = 0;
#ifdef _WIN32
    if (gc->heap_handle) {
        HeapDestroy(gc->heap_handle);
        gc->heap_handle = NULL;
    }
#endif
}

void* kgc_alloc(KGC* gc, size_t size, KObjType type) {
    // 觸發策略：如果當前分配量超過閾值，則觸發 GC
    if (gc->bytes_allocated > gc->next_gc_threshold) {
        kgc_collect(gc);
    }

    // 計算總大小：即請求的大小 (調用者負責傳入結構體的總大小)
    size_t total_size = size;
    
#ifdef _WIN32
    KObjHeader* header = (KObjHeader*)HeapAlloc(gc->heap_handle, HEAP_ZERO_MEMORY, total_size);
#else
    KObjHeader* header = (KObjHeader*)malloc(total_size);
#endif

    if (header == NULL) {
        // 嘗試緊急 GC
        kgc_collect(gc);
#ifdef _WIN32
        header = (KObjHeader*)HeapAlloc(gc->heap_handle, HEAP_ZERO_MEMORY, total_size);
#else
        header = (KObjHeader*)malloc(total_size);
#endif
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
    
    // Sync vm->objects for legacy code support
    if (gc->vm) {
        gc->vm->objects = header;
    }

    gc->bytes_allocated += total_size;

    // 清零數據區 (從頭部之後開始)
    // HeapAlloc with HEAP_ZERO_MEMORY already zeroed it on Windows.
#ifndef _WIN32
    if (size > sizeof(KObjHeader)) {
        void* data_ptr = (void*)(header + 1);
        memset(data_ptr, 0, size - sizeof(KObjHeader));
    }
#endif

#ifdef DEBUG_GC
    printf("[KGC] Alloc type %d, size %zu at %p. Header type set to %d\n", type, size, header, header->type);
#endif

    return header;
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
    return (KObjHeader*)ptr;
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
    if (value.type != VAL_OBJ) return;
    if (value.as.obj == NULL) return;
    KObjHeader* header = kgc_get_header(value.as.obj);
    kgc_mark_obj(gc, header);
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
    for (int i = 0; i < vm->globals.capacity; i++) {
        if (vm->globals.entries[i].key != NULL) {
            kgc_mark_value(gc, vm->globals.entries[i].value);
        }
    }

    // 5. 標記模塊
    for (int i = 0; i < vm->modules.capacity; i++) {
        if (vm->modules.entries[i].key != NULL) {
            kgc_mark_value(gc, vm->modules.entries[i].value);
        }
    }
}

static void blacken_object(KGC* gc, KObjHeader* obj) {
    // 根據對象類型，遍歷其內部引用的對象
    switch (obj->type) {
        case OBJ_STRING:
            // 字符串不引用其他對象
            break;
        case OBJ_CLASS_INSTANCE: {
            KObjInstance* instance = (KObjInstance*)obj;
            // Mark fields table
            for (int i = 0; i < instance->fields.capacity; i++) {
                if (instance->fields.entries[i].key != NULL) {
                    kgc_mark_value(gc, instance->fields.entries[i].value);
                }
            }
            // Mark class reference
            if (instance->klass) {
                kgc_mark_obj(gc, (KObjHeader*)instance->klass);
            }
            break;
        }
        case OBJ_ARRAY: {
            KObjArray* array = (KObjArray*)obj;
            for (int i = 0; i < array->length; i++) {
                kgc_mark_value(gc, array->elements[i]);
            }
            break;
        }
        case OBJ_CLASS: {
             KObjClass* klass = (KObjClass*)obj;
             // Mark methods
             for (int i = 0; i < klass->methods.capacity; i++) {
                 if (klass->methods.entries[i].key != NULL) {
                     kgc_mark_value(gc, klass->methods.entries[i].value);
                 }
             }
             // Mark parent
             if (klass->parent) {
                 kgc_mark_obj(gc, (KObjHeader*)klass->parent);
             }
             break;
        }
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
            
            // 釋放對象持有的資源
            switch (unreached->type) {
                case OBJ_STRING: {
                    KObjString* str = (KObjString*)unreached;
                    if (str->chars) free(str->chars);
                    break;
                }
                case OBJ_ARRAY: {
                    KObjArray* arr = (KObjArray*)unreached;
                    if (arr->elements) free(arr->elements);
                    break;
                }
                case OBJ_CLASS_INSTANCE: {
                    KObjInstance* ins = (KObjInstance*)unreached;
                    free_table(&ins->fields);
                    break;
                }
                case OBJ_CLASS: {
                    KObjClass* cls = (KObjClass*)unreached;
                    free_table(&cls->methods);
                    if (cls->name) free(cls->name);
                    break;
                }
                case OBJ_FUNCTION: {
                    KObjFunction* func = (KObjFunction*)unreached;
                    if (func->name) free(func->name);
                    break;
                }
                case OBJ_NATIVE: {
                    KObjNative* nat = (KObjNative*)unreached;
                    if (nat->name) free(nat->name);
                    break;
                }
                default: break;
            }

#ifdef _WIN32
            HeapFree(gc->heap_handle, 0, unreached);
#else
            free(unreached);
#endif
        }
    }
}
