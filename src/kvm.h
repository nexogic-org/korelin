#ifndef KORELIN_KVM_H
#define KORELIN_KVM_H

#include "kcode.h"
#include <stdint.h>
#include <stdbool.h>

// Forward Decl for GC
struct KGC;
typedef struct KGC KGC;
typedef struct ComeOnJIT ComeOnJIT;

#define MAX_NATIVE_ARGS 16

/**
 * @brief 值類型枚舉
 */
typedef enum {
    VAL_NULL,
    VAL_BOOL,
    VAL_INT,
    VAL_FLOAT,
    VAL_DOUBLE,
    VAL_OBJ,    /**< 對象指針 */
    VAL_STRING  /**< 字符串指針 */
} KValueType;

/* --- Object Structures --- */
typedef enum {
    OBJ_STRING,
    OBJ_STRUCT,
    OBJ_CLASS,
    OBJ_CLASS_INSTANCE,
    OBJ_ARRAY,
    OBJ_FUNCTION,
    OBJ_UPVALUE,
    OBJ_NATIVE,      /**< Native C Function */
    OBJ_BOUND_METHOD
} KObjType;

/**
 * @brief 對象頭部 (GC 使用)
 */
typedef struct KObjHeader {
    struct KObjHeader* next;
    bool marked;
    KObjType type;
    size_t size;
} KObjHeader;

/**
 * @brief 本地函數類型
 */
typedef void (*NativeFunc)();

/**
 * @brief 本地函數對象
 */
typedef struct {
    KObjHeader header;
    NativeFunc function;
    char* name;
} KObjNative;

/**
 * @brief 字符串對象
 */
typedef struct {
    KObjHeader header;
    char* chars;
    int length;
    uint32_t hash;
} KObjString;

/**
 * @brief 通用對象包裝器
 */
typedef struct {
    KObjHeader header;
} KObj;

/**
 * @brief 值定義
 */
typedef struct KValue {
    KValueType type;
    union {
        bool boolean;
        int64_t integer;
        float single_prec;
        double double_prec;
        void* obj; /**< 指向 KObjHeader */
        char* str;
    } as;
} KValue;

/**
 * @brief 哈希表條目
 */
typedef struct {
    char* key;
    KValue value;
} KTableEntry;

/**
 * @brief 哈希表 (Table)
 */
typedef struct {
    int count;
    int capacity;
    KTableEntry* entries;
} KTable;

void init_table(KTable* table);
void free_table(KTable* table);
bool table_set(KTable* table, const char* key, KValue value);
bool table_get(KTable* table, const char* key, KValue* value);

/**
 * @brief 前置聲明
 */
struct KObjClass;
struct KObjInstance;

/**
 * @brief 實例對象
 */
typedef struct KObjInstance {
    KObjHeader header;
    KTable fields;
    struct KObjClass* klass;
} KObjInstance;

/**
 * @brief 類對象
 */
typedef struct KObjClass {
    KObjHeader header;
    char* name;
    struct KObjClass* parent;
    KTable methods;
} KObjClass;

/**
 * @brief 函數對象
 */
typedef struct {
    KObjHeader header;
    int arity;
    KBytecodeChunk* chunk; /**< 指向字節碼塊 */
    uint32_t entry_point;  /**< 字節碼塊中的入口點 */
    char* name;
    int access;            /**< 0: private, 1: protected, 2: public */
    struct KObjClass* parent_class;
    struct KObjInstance* module;
} KObjFunction;

/**
 * @brief 綁定方法對象
 */
typedef struct {
    KObjHeader header;
    KValue receiver;
    KObjFunction* method;
} KObjBoundMethod;

/**
 * @brief 數組對象
 */
typedef struct {
    KObjHeader header;
    int length;
    int capacity;
    KValue* elements; /**< 指向 KValue 數組的指針 */
} KObjArray;

// --- VM Structure ---

#define KVM_STACK_SIZE 4096
#define KVM_MAX_FRAMES 64
#define KVM_REGISTERS_MAX 256

/**
 * @brief 異常幀
 */
typedef struct {
    uint8_t* handler_ip;
    int stack_depth;
    int frame_depth;
} ExceptionFrame;

/**
 * @brief 調用幀 (Call Frame)
 */
typedef struct {
    KBytecodeChunk* chunk;
    uint8_t* ip;
    KValue* base_registers;
    int return_reg;
    struct KObjInstance* module;
    KObjFunction* function;
} CallFrame;

// --- VM Structure ---

/**
 * @brief 虛擬機結構體
 */
typedef struct KVM {
    KBytecodeChunk* chunk;
    uint8_t* ip;
    KValue* registers;
    KValue stack[KVM_STACK_SIZE];
    KValue* stack_top;
    KValue* native_args; /**< 指向當前本地調用的參數的指針 */
    int native_argc;     /**< 當前本地調用的參數數量 */
    
    /* 調用幀 */
    CallFrame frames[KVM_MAX_FRAMES];
    int frame_count;
    bool had_error;
    
    /* 異常處理 */
    ExceptionFrame exception_frames[KVM_MAX_FRAMES];
    int exception_frame_count;
    KValue current_exception;

    /* GC 根 */
    KObjHeader* objects; /**< 所有對象的鏈表 */
    KGC* gc;

    /* 全局變量 */
    KTable globals;
    
    /* 模塊 */
    KTable modules;
    KTable lib_paths;
    
    /* 導入回調 */
    KValue (*import_handler)(struct KVM* vm, const char* name);
    KObjInstance* current_module;
    
    /* 環境 */
    char* root_dir;
    
    /* JIT (即時編譯) */
    ComeOnJIT* jit;

} KVM;

// --- API ---

/**
 * @brief 初始化虛擬機
 */
void kvm_init(KVM* vm);

/**
 * @brief 釋放虛擬機資源
 */
void kvm_free(KVM* vm);

/**
 * @brief 解釋執行字節碼塊
 */
int kvm_interpret(KVM* vm, KBytecodeChunk* chunk);

/**
 * @brief 運行 VM (低級接口)
 */
int kvm_run(KVM* vm);

/**
 * @brief 輔助：打印值
 */
void kvm_print_value(KValue value);

/**
 * @brief 調用函數
 */
bool kvm_call_function(KVM* vm, KObjFunction* function, int arg_count);

/**
 * @brief 壓入棧
 */
void kvm_push(KVM* vm, KValue value);

/**
 * @brief 彈出棧
 */
KValue kvm_pop(KVM* vm);

#endif //KORELIN_KVM_H
