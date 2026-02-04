//
// Created by Helix on 2026/1/10.
//

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

// --- 類型定義 ---

typedef enum {
    VAL_NULL,
    VAL_BOOL,
    VAL_INT,
    VAL_FLOAT,
    VAL_DOUBLE,
    VAL_OBJ, // Object pointer
    VAL_STRING // String pointer
} KValueType;

// --- Object Structures ---
typedef enum {
    OBJ_STRING,
    OBJ_STRUCT,
    OBJ_CLASS,
    OBJ_CLASS_INSTANCE,
    OBJ_ARRAY,
    OBJ_FUNCTION,
    OBJ_UPVALUE,
    OBJ_NATIVE, // Native C Function
    OBJ_BOUND_METHOD
} KObjType;

typedef struct KObjHeader {
    struct KObjHeader* next;
    bool marked;
    KObjType type;
    size_t size;
} KObjHeader;

// Native Function Type
typedef void (*NativeFunc)();

typedef struct {
    KObjHeader header;
    NativeFunc function;
    char* name;
} KObjNative;

// String Object
typedef struct {
    KObjHeader header;
    char* chars;
    int length;
    uint32_t hash;
} KObjString;

// Generic Object Wrapper
typedef struct {
    KObjHeader header;
} KObj;

// --- Value Definition ---
typedef struct KValue {
    KValueType type;
    union {
        bool boolean;
        int64_t integer;
        float single_prec;
        double double_prec;
        void* obj; // Points to KObjHeader
        char* str;
    } as;
} KValue;

// --- Table (Hash Map) ---
typedef struct {
    char* key;
    KValue value;
} KTableEntry;

typedef struct {
    int count;
    int capacity;
    KTableEntry* entries;
} KTable;

void init_table(KTable* table);
void free_table(KTable* table);
bool table_set(KTable* table, const char* key, KValue value);
bool table_get(KTable* table, const char* key, KValue* value);

// Forward Decl
struct KObjClass;
struct KObjInstance;

// Instance Object
typedef struct KObjInstance {
    KObjHeader header;
    KTable fields;
    struct KObjClass* klass;
} KObjInstance;

// Class Object
typedef struct KObjClass {
    KObjHeader header;
    char* name;
    struct KObjClass* parent;
    KTable methods;
} KObjClass;

// Function Object
typedef struct {
    KObjHeader header;
    int arity;
    KBytecodeChunk* chunk; // Changed to pointer
    uint32_t entry_point; // Entry point in the chunk
    char* name;
    int access; // 0: private, 1: protected, 2: public
    struct KObjClass* parent_class;
    struct KObjInstance* module;
} KObjFunction;

// Bound Method Object
typedef struct {
    KObjHeader header;
    KValue receiver;
    KObjFunction* method;
} KObjBoundMethod;

// --- Array Object ---
typedef struct {
    KObjHeader header;
    int length;
    int capacity;
    KValue* elements; // Pointer to KValue array
} KObjArray;

// --- VM Structure ---

#define KVM_STACK_SIZE 4096
#define KVM_MAX_FRAMES 64
#define KVM_REGISTERS_MAX 256

// Exception Frame
typedef struct {
    uint8_t* handler_ip;
    int stack_depth;
    int frame_depth;
} ExceptionFrame;

// 調用幀 (Call Frame)
typedef struct {
    KBytecodeChunk* chunk;
    uint8_t* ip;
    KValue* base_registers;
    int return_reg;
    struct KObjInstance* module;
    KObjFunction* function;
} CallFrame;

// --- VM Structure ---
typedef struct KVM {
    KBytecodeChunk* chunk;
    uint8_t* ip;
    KValue* registers;
    KValue stack[KVM_STACK_SIZE];
    KValue* stack_top;
    KValue* native_args; // Pointer to arguments for current native call
    int native_argc; // Argument count for current native call
    
    // Call Frames
    CallFrame frames[KVM_MAX_FRAMES];
    int frame_count;
    bool had_error;
    
    // Exception Handling
    ExceptionFrame exception_frames[KVM_MAX_FRAMES];
    int exception_frame_count;
    KValue current_exception;

    // GC Roots
    KObjHeader* objects; // Linked list of all objects
    KGC* gc;

    // Globals
    KTable globals;
    
    // Modules
    KTable modules;
    KTable lib_paths;
    
    // Import Callback
    KValue (*import_handler)(struct KVM* vm, const char* name);
    KObjInstance* current_module;
    
    // Environment
    char* root_dir;
    
    // JIT
    ComeOnJIT* jit;

} KVM;

// --- API ---

// 初始化虛擬機
void kvm_init(KVM* vm);

// 釋放虛擬機資源
void kvm_free(KVM* vm);

// 解釋執行字節碼塊
int kvm_interpret(KVM* vm, KBytecodeChunk* chunk);

// 運行 VM (低級接口)
int kvm_run(KVM* vm);

// 輔助：打印值
void kvm_print_value(KValue value);

// Call function
bool kvm_call_function(KVM* vm, KObjFunction* function, int arg_count);

// Stack Ops
void kvm_push(KVM* vm, KValue value);
KValue kvm_pop(KVM* vm);

#endif //KORELIN_KVM_H
