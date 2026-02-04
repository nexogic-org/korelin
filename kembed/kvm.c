//
// Created by Helix on 2026/1/10.
//

#include "kvm.h"
#include "kcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Move macro definitions to top or define before usage
#define REG(idx) (vm->registers[idx])

// Forward Declarations
static bool call_value(KVM* vm, KValue callee, int arg_count, int return_reg);
static bool call(KVM* vm, KObjFunction* function, int arg_count, int return_reg);

// --- Table Implementation ---
void init_table(KTable* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void free_table(KTable* table) {
    free(table->entries);
    init_table(table);
}

static uint32_t hash_string(const char* key) {
    uint32_t hash = 2166136261u;
    for (int i = 0; key[i]; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static KTableEntry* find_entry(KTableEntry* entries, int capacity, const char* key) {
    uint32_t index = hash_string(key) % capacity;
    for (;;) {
        KTableEntry* entry = &entries[index];
        if (entry->key == NULL || strcmp(entry->key, key) == 0) {
            return entry;
        }
        index = (index + 1) % capacity;
    }
}

static void adjust_capacity(KTable* table, int capacity) {
    KTableEntry* entries = (KTableEntry*)calloc(capacity, sizeof(KTableEntry));
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value.type = VAL_NULL;
    }
    
    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        KTableEntry* entry = &table->entries[i];
        if (entry->key == NULL) continue;
        
        KTableEntry* dest = find_entry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }
    
    free(table->entries);
    table->entries = entries;
    table->capacity = capacity;
}

bool table_set(KTable* table, const char* key, KValue value) {
    if (table->count + 1 > table->capacity * 0.75) {
        int capacity = table->capacity < 8 ? 8 : table->capacity * 2;
        adjust_capacity(table, capacity);
    }
    
    KTableEntry* entry = find_entry(table->entries, table->capacity, key);
    bool is_new_key = entry->key == NULL;
    if (is_new_key) {
        table->count++;
        entry->key = strdup(key); // Assuming key ownership needs to be taken or copy
    }
    entry->value = value;
    return is_new_key;
}

bool table_get(KTable* table, const char* key, KValue* value) {
    if (table->count == 0) return false;
    KTableEntry* entry = find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;
    *value = entry->value;
    return true;
}

// Placeholder for KFunction call
static bool call(KVM* vm, KObjFunction* function, int arg_count, int return_reg) {
    // Access Check
    if (function->access == 1) { // Private
        bool allowed = false;
        if (vm->frame_count > 0) {
             CallFrame* caller = &vm->frames[vm->frame_count - 1];
             if (caller->function && caller->function->parent_class == function->parent_class) {
                 allowed = true;
             }
        }
        
        if (!allowed) {
             printf("Runtime Error: Cannot access private member '%s'\n", function->name);
             vm->had_error = true;
             return false;
        }
    } else if (function->access == 2) { // Protected
        bool allowed = false;
        if (vm->frame_count > 0) {
             CallFrame* caller = &vm->frames[vm->frame_count - 1];
             if (caller->function) {
                 KObjClass* caller_class = caller->function->parent_class;
                 KObjClass* target_class = function->parent_class;
                 
                 // Check if caller_class is subclass of target_class
                 KObjClass* curr = caller_class;
                 while (curr) {
                     if (curr == target_class) {
                         allowed = true;
                         break;
                     }
                     curr = curr->parent;
                 }
             }
        }
        
        if (!allowed) {
             printf("Runtime Error: Cannot access protected member '%s'\n", function->name);
             vm->had_error = true;
             return false;
        }
    }

    if (arg_count != function->arity) {
        printf("Runtime Error: Expected %d arguments but got %d.\n", function->arity, arg_count);
        vm->had_error = true;
        return false;
    }

    if (vm->frame_count >= KVM_MAX_FRAMES) {
        printf("Runtime Error: Stack overflow.\n");
        vm->had_error = true;
        return false;
    }

    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->chunk = vm->chunk;
    frame->ip = vm->ip;
    frame->base_registers = vm->registers;
    frame->return_reg = return_reg;
    frame->function = function;
    frame->module = vm->current_module; // Save caller's module
    
    // Switch to callee's module context
    if (function->module) {
        vm->current_module = function->module;
        vm->globals = function->module->fields;
    }
    
    vm->chunk = function->chunk;
    vm->ip = function->chunk->code + function->entry_point;
    
    // New register window starts at arguments
    vm->registers = vm->stack_top - arg_count;
    // Reserve space for locals
    vm->stack_top = vm->registers + 64; 

    /* JIT Disabled
    // Try JIT Compilation for the called function
    if (vm->jit.enabled && !vm->chunk->jit_code) {
        vm->chunk->jit_code = jit_compile(&vm->jit, vm->chunk);
    }

    if (vm->chunk->jit_code) {
        // ...
        return true;
    }
    */
    
    return true; 
}

// static bool call_value is defined previously
static bool call_value(KVM* vm, KValue callee, int arg_count, int return_reg) {
    if (callee.type == VAL_OBJ) {
        switch (((KObj*)callee.as.obj)->header.type) { // Fix: cast to KObj*
            case OBJ_FUNCTION:
                return call(vm, (KObjFunction*)callee.as.obj, arg_count, return_reg);

            case OBJ_BOUND_METHOD: {
                KObjBoundMethod* bound = (KObjBoundMethod*)callee.as.obj;
                
                if (vm->stack_top + 1 - vm->stack >= KVM_STACK_SIZE) {
                    printf("Stack overflow\n");
                    return false;
                }
                
                // Shift arguments to make room for receiver
                vm->stack_top++;
                for (int i = 0; i < arg_count; i++) {
                    *(vm->stack_top - 1 - i) = *(vm->stack_top - 2 - i);
                }
                *(vm->stack_top - arg_count - 1) = bound->receiver;
                
                KObjHeader* method_header = (KObjHeader*)bound->method;
                if (method_header->type == OBJ_NATIVE) {
                    // Handle Bound Native Method
                    KObjNative* native = (KObjNative*)bound->method;
                    NativeFunc func = native->function;
                    vm->native_args = vm->stack_top - (arg_count + 1);
                    vm->native_argc = arg_count + 1;
                    func();
                    
                    // Cleanup
                    KValue result = *(vm->stack_top - 1);
                    vm->stack_top--; // Pop result
                    vm->stack_top -= (arg_count + 1); // Pop args + receiver
                    
                    if (return_reg != -1) {
                        REG(return_reg) = result;
                    }
                    return true;
                } else {
                    return call(vm, bound->method, arg_count + 1, return_reg);
                }
            }
            
            case OBJ_NATIVE: {
                NativeFunc func = ((KObjNative*)callee.as.obj)->function;
                vm->native_args = vm->stack_top - arg_count;
                vm->native_argc = arg_count;
                func();
                
                // Stack cleanup:
                // Native func pushed 1 result (or void/null)
                // Result is at stack_top - 1
                // Wait, native functions use KReturnXXX which pushes to stack?
                // Yes, KReturnVoid pushes null? No, KReturnVoid might push nothing?
                // Let's assume standard is 1 result.
                // If KReturnVoid does nothing, stack_top is same.
                // But we need to cleanup args.
                
                // NOTE: We rely on native functions to leave exactly 1 result on stack if they return value?
                // KReturnXXX pushes 1 value.
                // KReturnVoid pushes nothing?
                // Let's check kapi.h or usage.
                // Standard convention: Native function should push 1 result.
                // If KReturnVoid pushes VAL_NULL, then we pop 1.
                // If KReturnVoid pushes nothing, we need to push NULL.
                
                // Assuming result is on stack top.
                KValue result = *(vm->stack_top - 1);
                vm->stack_top--; // Pop result
                
                vm->stack_top -= arg_count; // Pop args
                
                // Store result in return_reg if valid
                if (return_reg != -1) {
                    REG(return_reg) = result;
                }
                
                return true;
            }

            case OBJ_CLASS_INSTANCE: {
                // Class init or call operator?
                // For now, assume not supported
                printf("Call on instance not supported yet\n");
                return false;
            }
            default:
                break;
        }
    }
    printf("Attempt to call non-callable value. Type: %d\n", callee.type);
    if (callee.type == VAL_OBJ) printf("Obj Type: %d\n", ((KObj*)callee.as.obj)->header.type);
    return false;
}

void kvm_push(KVM* vm, KValue value) {
    if (vm->stack_top - vm->stack >= KVM_STACK_SIZE) {
        printf("Stack overflow\n");
        vm->had_error = true;
        return;
    }
    *vm->stack_top = value;
    vm->stack_top++;
}

KValue kvm_pop(KVM* vm) {
    if (vm->stack_top == vm->stack) {
        printf("Stack underflow\n");
        vm->had_error = true;
        KValue v; v.type = VAL_NULL; return v;
    }
    vm->stack_top--;
    return *vm->stack_top;
}

#define READ_BYTE() (*vm->ip++)
#define READ_REG_IDX() (*vm->ip++)
#define READ_IMM8() ((int8_t)(*vm->ip++))
#define READ_SHORT() (vm->ip += 2, (uint16_t)((vm->ip[-2] << 8) | vm->ip[-1]))

// 讀取 16 位立即數 (大端序，因為 kcode.c 是高位在前)
#define READ_IMM16() \
    (vm->ip += 2, (uint16_t)((vm->ip[-2] << 8) | vm->ip[-1]))

// 讀取 24 位地址 (用於跳轉/調用)
#define READ_IMM24() \
    (vm->ip += 3, (uint32_t)((vm->ip[-3] << 16) | (vm->ip[-2] << 8) | vm->ip[-1]))

// REG moved to top

#define REG_AS_INT(idx) (vm->registers[idx].as.integer)
#define REG_AS_DOUBLE(idx) (vm->registers[idx].as.double_prec)

#define RUNTIME_ERROR(msg) \
    { \
        if (!throw_runtime_error_obj(vm, "RuntimeError", msg)) { \
            printf("Runtime Error: %s\n", msg); \
            vm->had_error = true; \
            return 1; \
        } \
        break; \
    }

#define THROW_ERROR(type, msg) \
    { \
        if (!throw_runtime_error_obj(vm, type, msg)) { \
            printf("Runtime Error (%s): %s\n", type, msg); \
            vm->had_error = true; \
            return 1; \
        } \
        break; \
    }

#define BINARY_OP_INT(op) \
    do { \
        uint8_t rd = READ_REG_IDX(); \
        uint8_t ra = READ_REG_IDX(); \
        uint8_t rb = READ_REG_IDX(); \
        if (REG(ra).type != VAL_INT || REG(rb).type != VAL_INT) { \
            printf("Type Error: Ra=%d, Rb=%d\n", REG(ra).type, REG(rb).type); \
            RUNTIME_ERROR("Operands must be integers"); \
        } \
        REG(rd).type = VAL_INT; \
        REG(rd).as.integer = REG_AS_INT(ra) op REG_AS_INT(rb); \
    } while(0)

#define BINARY_OP_DOUBLE(op) \
    do { \
        uint8_t rd = READ_REG_IDX(); \
        uint8_t ra = READ_REG_IDX(); \
        uint8_t rb = READ_REG_IDX(); \
        double va = (REG(ra).type == VAL_FLOAT) ? REG(ra).as.single_prec : REG_AS_DOUBLE(ra); \
        double vb = (REG(rb).type == VAL_FLOAT) ? REG(rb).as.single_prec : REG_AS_DOUBLE(rb); \
        REG(rd).type = VAL_DOUBLE; \
        REG(rd).as.double_prec = va op vb; \
    } while(0)

// 比較運算宏 (通用)
static bool values_equal(KValue a, KValue b) {
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return a.as.integer == b.as.integer;
    }
    if (a.type == VAL_NULL && b.type == VAL_NULL) return true;
    if (a.type == VAL_BOOL && b.type == VAL_BOOL) return a.as.boolean == b.as.boolean;
    
    // Numbers
    if ((a.type == VAL_INT || a.type == VAL_FLOAT || a.type == VAL_DOUBLE) &&
        (b.type == VAL_INT || b.type == VAL_FLOAT || b.type == VAL_DOUBLE)) {
        double da = (a.type == VAL_INT) ? (double)a.as.integer : (a.type == VAL_FLOAT ? a.as.single_prec : a.as.double_prec);
        double db = (b.type == VAL_INT) ? (double)b.as.integer : (b.type == VAL_FLOAT ? b.as.single_prec : b.as.double_prec);
        return da == db;
    }
    
    // Strings
    if (a.type == VAL_STRING && b.type == VAL_STRING) return strcmp(a.as.str, b.as.str) == 0;
    
    // Objects (Strings)
    if (a.type == VAL_OBJ || b.type == VAL_OBJ) {
        // Unpack objects if they are strings
        char* sa = NULL;
        char* sb = NULL;
        
        if (a.type == VAL_STRING) sa = a.as.str;
        else if (a.type == VAL_OBJ && ((KObj*)a.as.obj)->header.type == OBJ_STRING) sa = ((KObjString*)a.as.obj)->chars;
        
        if (b.type == VAL_STRING) sb = b.as.str;
        else if (b.type == VAL_OBJ && ((KObj*)b.as.obj)->header.type == OBJ_STRING) sb = ((KObjString*)b.as.obj)->chars;
        
        if (sa && sb) {
             return strcmp(sa, sb) == 0;
        }
        
        // General object identity
        if (a.type == VAL_OBJ && b.type == VAL_OBJ) return a.as.obj == b.as.obj;
    }
    
    return false;
}

#define CMP_OP_NUM(op) \
    do { \
        uint8_t rd = READ_REG_IDX(); \
        uint8_t ra = READ_REG_IDX(); \
        uint8_t rb = READ_REG_IDX(); \
        KValue va = REG(ra); \
        KValue vb = REG(rb); \
        REG(rd).type = VAL_BOOL; \
        if ((va.type == VAL_INT || va.type == VAL_FLOAT || va.type == VAL_DOUBLE) && \
            (vb.type == VAL_INT || vb.type == VAL_FLOAT || vb.type == VAL_DOUBLE)) { \
            double da = (va.type == VAL_INT) ? (double)va.as.integer : (va.type == VAL_FLOAT ? va.as.single_prec : va.as.double_prec); \
            double db = (vb.type == VAL_INT) ? (double)vb.as.integer : (vb.type == VAL_FLOAT ? vb.as.single_prec : vb.as.double_prec); \
            REG(rd).as.boolean = da op db; \
        } else { \
             /* Default for non-numbers (e.g. strings) in inequality? Maybe throw error or false */ \
             REG(rd).as.boolean = false; \
        } \
    } while(0)

#define CMP_OP_INT(op) CMP_OP_NUM(op)

// Helper for string concat
static KObjString* alloc_string(KVM* vm, const char* chars, int length) {
    KObjString* str = (KObjString*)malloc(sizeof(KObjString));
    str->header.type = OBJ_STRING;
    str->header.marked = false;
    str->header.next = vm->objects;
    str->header.size = sizeof(KObjString) + length + 1;
    vm->objects = (KObjHeader*)str;
    
    str->length = length;
    str->chars = (char*)malloc(length + 1);
    memcpy(str->chars, chars, length);
    str->chars[length] = '\0';
    str->hash = 0; 
    return str;
}

static char* value_to_string_kvm(KValue v) {
    char buf[64];
    if (v.type == VAL_INT) { sprintf(buf, "%lld", v.as.integer); return strdup(buf); }
    if (v.type == VAL_FLOAT) { sprintf(buf, "%f", v.as.single_prec); return strdup(buf); }
    if (v.type == VAL_DOUBLE) { sprintf(buf, "%lf", v.as.double_prec); return strdup(buf); }
    if (v.type == VAL_BOOL) { return strdup(v.as.boolean ? "true" : "false"); }
    if (v.type == VAL_NULL) { return strdup("null"); }
    if (v.type == VAL_STRING) { return strdup(v.as.str); }
    if (v.type == VAL_OBJ) {
        KObj* obj = (KObj*)v.as.obj;
        if (obj->header.type == OBJ_STRING) return strdup(((KObjString*)obj)->chars);
        return strdup("[Object]");
    }
    return strdup("");
}

// --- 初始化與清理 ---

void kvm_init(KVM* vm) {
    vm->chunk = NULL;
    vm->ip = NULL;
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->exception_frame_count = 0; // Init exception stack
    vm->current_exception.type = VAL_NULL;
    vm->had_error = false;
    
    // Set initial registers to point to start of stack
    vm->registers = vm->stack;
    // Reserve space for registers
    vm->stack_top += KVM_REGISTERS_MAX;

    // 初始化寄存器為 0/NULL
    for (int i = 0; i < KVM_REGISTERS_MAX; i++) {
        vm->registers[i].type = VAL_INT;
        vm->registers[i].as.integer = 0;
    }
    
    init_table(&vm->globals);
    init_table(&vm->modules);
    vm->current_module = NULL;
    // // jit_init(&vm->jit);
    vm->import_handler = NULL;
    vm->root_dir = NULL;
}

void kvm_free(KVM* vm) {
    free_table(&vm->globals);
    free_table(&vm->modules);
    // // jit_cleanup(&vm->jit);
    // 釋放任何動態分配的資源 (如果有的話)
}

void kvm_print_value(KValue value) {
    switch (value.type) {
        case VAL_INT: printf("%lld", value.as.integer); break;
        case VAL_FLOAT: printf("%f", value.as.single_prec); break;
        case VAL_DOUBLE: printf("%lf", value.as.double_prec); break;
        case VAL_BOOL: printf(value.as.boolean ? "true" : "false"); break;
        case VAL_NULL: printf("null"); break;
        case VAL_STRING: printf("%s", value.as.str); break;
        case VAL_OBJ: printf("<obj %p>", value.as.obj); break;
    }
}

// --- 解釋器核心 ---

static bool propagate_exception(KVM* vm) {
    while (vm->exception_frame_count > 0) {
        ExceptionFrame* frame = &vm->exception_frames[vm->exception_frame_count - 1];
        
        // Unwind call stack
        while (vm->frame_count > frame->frame_depth) {
            vm->frame_count--;
            if (vm->frame_count > 0) {
                vm->registers = vm->frames[vm->frame_count - 1].base_registers;
                vm->chunk = vm->frames[vm->frame_count - 1].chunk;
            } else {
                vm->registers = vm->stack;
                // vm->chunk is not restored here, assuming top level chunk is handled or execution ends
            }
        }
        
        // Restore stack
        vm->stack_top = vm->stack + frame->stack_depth;
        
        // Jump to handler
        vm->ip = frame->handler_ip;
        
        // Pop exception frame
        vm->exception_frame_count--;
        
        return true;
    }
    return false;
}

static bool throw_runtime_error_obj(KVM* vm, const char* type, const char* msg) {
    KValue class_val;
    KObjClass* klass = NULL;
    
    if (table_get(&vm->globals, type, &class_val) && class_val.type == VAL_OBJ) {
        klass = (KObjClass*)class_val.as.obj;
    }
    
    KObjInstance* ex = (KObjInstance*)malloc(sizeof(KObjInstance));
    ex->header.type = OBJ_CLASS_INSTANCE;
    ex->header.marked = false;
    ex->header.next = vm->objects;
    ex->header.size = sizeof(KObjInstance);
    vm->objects = (KObjHeader*)ex;
    ex->klass = klass;
    init_table(&ex->fields);
    
    // TODO: Set message
    
    vm->current_exception.type = VAL_OBJ;
    vm->current_exception.as.obj = (KObj*)ex;
    
    return propagate_exception(vm);
}

// Helper to resolve dotted names (e.g. "thread.create")
static bool resolve_dotted_name(KVM* vm, const char* name, KValue* out_val) {
    char* name_copy = strdup(name);
    char* token = strtok(name_copy, ".");
    KValue current_val;
    
    if (!table_get(&vm->globals, token, &current_val)) {
        // Try module
        if (!table_get(&vm->modules, token, &current_val)) {
             free(name_copy);
             return false;
        }
    }
    
    token = strtok(NULL, ".");
    while (token != NULL) {
        if (current_val.type != VAL_OBJ) {
            free(name_copy);
            return false;
        }
        
        KObj* obj = (KObj*)current_val.as.obj;
        bool found = false;
        KValue next_val;
        
        if (obj->header.type == OBJ_CLASS_INSTANCE) {
            KObjInstance* inst = (KObjInstance*)obj;
            if (table_get(&inst->fields, token, &next_val)) found = true;
            else if (inst->klass && table_get(&inst->klass->methods, token, &next_val)) found = true;
        } else if (obj->header.type == OBJ_CLASS) {
            KObjClass* klass = (KObjClass*)obj;
            if (table_get(&klass->methods, token, &next_val)) found = true;
        } else if (obj->header.type == OBJ_FUNCTION || obj->header.type == OBJ_NATIVE) {
             // Function doesn't have fields usually
        }
        
        if (!found) {
            free(name_copy);
            return false;
        }
        
        current_val = next_val;
        token = strtok(NULL, ".");
    }
    
    free(name_copy);
    *out_val = current_val;
    return true;
}

int kvm_run(KVM* vm) {
    // 循環直到結束或錯誤
    for (;;) {
        uint8_t opcode = READ_BYTE();
        
        // 用於調試：打印執行的指令
        // printf("Exec: 0x%02X\n", opcode);

        switch (opcode) {
            // --- 2.1 算術與邏輯 ---
            case KOP_ADD: {
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX();
                uint8_t rb = READ_REG_IDX();
                
                KValue va = REG(ra);
                KValue vb = REG(rb);
                
                if (va.type == VAL_STRING || vb.type == VAL_STRING || 
                          (va.type == VAL_OBJ && ((KObj*)va.as.obj)->header.type == OBJ_STRING) ||
                          (vb.type == VAL_OBJ && ((KObj*)vb.as.obj)->header.type == OBJ_STRING)) {
                    // String concat (Highest priority for mixed types)
                    char* sa = value_to_string_kvm(va);
                    char* sb = value_to_string_kvm(vb);
                    
                    int len_a = strlen(sa);
                    int len_b = strlen(sb);
                    char* res = (char*)malloc(len_a + len_b + 1);
                    strcpy(res, sa);
                    strcat(res, sb);
                    
                    KObjString* ks = alloc_string(vm, res, len_a + len_b);
                    free(sa); free(sb); free(res);
                    
                    REG(rd).type = VAL_OBJ;
                    REG(rd).as.obj = ks;
                } else if (va.type == VAL_DOUBLE || vb.type == VAL_DOUBLE || 
                           va.type == VAL_FLOAT || vb.type == VAL_FLOAT) {
                    // Float add
                    double da = (va.type == VAL_INT) ? (double)va.as.integer : (va.type == VAL_FLOAT ? va.as.single_prec : va.as.double_prec);
                    double db = (vb.type == VAL_INT) ? (double)vb.as.integer : (vb.type == VAL_FLOAT ? vb.as.single_prec : vb.as.double_prec);
                    REG(rd).type = VAL_DOUBLE;
                    REG(rd).as.double_prec = da + db;
                } else if (va.type == VAL_INT && vb.type == VAL_INT) {
                    REG(rd).type = VAL_INT;
                    REG(rd).as.integer = va.as.integer + vb.as.integer;
                } else {
                    printf("Type Error: Ra=%d, Rb=%d\n", va.type, vb.type);
                    RUNTIME_ERROR("Operands must be numbers or strings");
                }
                break;
            }
            case KOP_SUB: BINARY_OP_INT(-); break;
            case KOP_MUL: BINARY_OP_INT(*); break;
            case KOP_DIV: {
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX();
                uint8_t rb = READ_REG_IDX();
                if (REG_AS_INT(rb) == 0) THROW_ERROR("DivisionByZeroError", "Division by zero");
                REG(rd).type = VAL_INT;
                REG(rd).as.integer = REG_AS_INT(ra) / REG_AS_INT(rb);
                break;
            }
            case KOP_MOD: {
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX();
                uint8_t rb = READ_REG_IDX();
                if (REG_AS_INT(rb) == 0) THROW_ERROR("DivisionByZeroError", "Modulo by zero");
                REG(rd).type = VAL_INT;
                REG(rd).as.integer = REG_AS_INT(ra) % REG_AS_INT(rb);
                break;
            }
            case KOP_NEG: {
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX();
                READ_BYTE(); // 填充
                if (REG(ra).type == VAL_INT) {
                    REG(rd).type = VAL_INT;
                    REG(rd).as.integer = -REG_AS_INT(ra);
                } else if (REG(ra).type == VAL_DOUBLE) {
                    REG(rd).type = VAL_DOUBLE;
                    REG(rd).as.double_prec = -REG_AS_DOUBLE(ra);
                }
                break;
            }

            // 比較運算
            case KOP_EQ: {
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX();
                uint8_t rb = READ_REG_IDX();
                KValue va = REG(ra);
                KValue vb = REG(rb);
                REG(rd).type = VAL_BOOL;
                REG(rd).as.boolean = values_equal(va, vb);
                break;
            }
            case KOP_NE: {
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX();
                uint8_t rb = READ_REG_IDX();
                KValue va = REG(ra);
                KValue vb = REG(rb);
                REG(rd).type = VAL_BOOL;
                REG(rd).as.boolean = !values_equal(va, vb);
                break;
            }
            case KOP_LT: CMP_OP_NUM(<); break;
            case KOP_LE: CMP_OP_NUM(<=); break;
            case KOP_GT: CMP_OP_NUM(>); break;
            case KOP_GE: CMP_OP_NUM(>=); break;
            
            // 立即數運算
            case KOP_ADDI: {
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX();
                int8_t imm = READ_IMM8();
                if (REG(ra).type == VAL_INT) {
                    REG(rd).type = VAL_INT;
                    REG(rd).as.integer = REG_AS_INT(ra) + imm;
                }
                break;
            }

            case KOP_LDI: { // LDI Rd, Imm8 (Load Immediate Integer)
                uint8_t rd = READ_REG_IDX();
                int8_t imm = READ_IMM8();
                READ_BYTE(); // Padding (was ra)
                REG(rd).type = VAL_INT;
                REG(rd).as.integer = imm;
                break;
            }

            case KOP_LDB: { // LDB Rd, Imm8
                uint8_t rd = READ_REG_IDX();
                int8_t imm = READ_IMM8();
                READ_BYTE(); // Padding
                REG(rd).type = VAL_BOOL;
                REG(rd).as.boolean = (imm != 0);
                break;
            }

            case KOP_LDI64: { // LDI64 Rd, Imm64
                uint8_t rd = READ_REG_IDX();
                
                uint64_t bits = 0;
                for(int i=0; i<8; i++) {
                     bits = (bits << 8) | READ_BYTE();
                }
                
                REG(rd).type = VAL_INT;
                REG(rd).as.integer = (int64_t)bits;
                break;
            }

            case KOP_MOVE: { // MOVE Rd, Ra
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX();
                READ_BYTE(); // Padding
                REG(rd) = REG(ra);
                break;
            }
            

            case KOP_AND: BINARY_OP_INT(&); break;
            case KOP_OR:  BINARY_OP_INT(|); break;
            case KOP_XOR: BINARY_OP_INT(^); break;
            
            // --- 2.2 浮點數 ---
            case KOP_FADD_D: BINARY_OP_DOUBLE(+); break;
            case KOP_FSUB_D: BINARY_OP_DOUBLE(-); break;
            case KOP_FMUL_D: BINARY_OP_DOUBLE(*); break;
            case KOP_FDIV_D: BINARY_OP_DOUBLE(/); break;
            
            // --- 2.3 內存與棧 ---
            case KOP_LOAD: { // LOAD Rd, Ra (Move)
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX();
                READ_BYTE(); // padding
                REG(rd) = REG(ra);
                break;
            }
            case KOP_PUSH: { // PUSH _ Ra _
                READ_BYTE(); // padding
                uint8_t ra = READ_REG_IDX();
                READ_BYTE(); // padding
                if (vm->stack_top - vm->stack >= KVM_STACK_SIZE) RUNTIME_ERROR("Stack overflow");
                *vm->stack_top++ = REG(ra);
                break;
            }
            case KOP_POP: { // POP Rd
                uint8_t rd = READ_REG_IDX();
                READ_BYTE(); READ_BYTE();
                if (vm->stack_top == vm->stack) RUNTIME_ERROR("Stack underflow");
                REG(rd) = *(--vm->stack_top);
                break;
            }
            
            // --- 2.4 控制流 ---
            case KOP_JMP: {
                READ_BYTE(); // Padding (was R1 in emit_jump)
                int16_t offset = (int16_t)READ_IMM16();
                vm->ip += offset; 
                break;
            }
            case KOP_JZ: {
                uint8_t ra = READ_REG_IDX();
                uint16_t offset = READ_IMM16(); // 相對偏移
                // 注意：kcode.c 實現中 JZ 格式是 Op(1) Rd(1) Imm(2). 
                // 所以這裡讀取 Ra (Condition), 然後讀取 Imm16.
                
                bool condition_false = false;
                if (REG(ra).type == VAL_BOOL && !REG(ra).as.boolean) condition_false = true;
                if (REG(ra).type == VAL_INT && REG(ra).as.integer == 0) condition_false = true;
                
                if (condition_false) {
                     vm->ip += (int16_t)offset; 
                }
                break;
            }
            case KOP_JNZ: { // 新增 JNZ
                uint8_t ra = READ_REG_IDX();
                uint16_t offset = READ_IMM16(); // 相對偏移
                
                bool condition_true = false;
                if (REG(ra).type == VAL_BOOL && REG(ra).as.boolean) condition_true = true;
                if (REG(ra).type == VAL_INT && REG(ra).as.integer != 0) condition_true = true;
                
                if (condition_true) {
                     vm->ip += (int16_t)offset; 
                }
                break;
            }
            
            case KOP_CALLR: { // CALLR Rd, ArgCount
                uint8_t rd = READ_REG_IDX();
                uint8_t arg_count = READ_BYTE();
                READ_BYTE(); // Padding
                
                if (vm->frame_count >= KVM_MAX_FRAMES) RUNTIME_ERROR("Stack overflow");
                
                // Pass rd as return_reg
                if (!call_value(vm, REG(rd), arg_count, rd)) {
                    RUNTIME_ERROR("Call failed");
                }
                
                // Result handling is moved to call_value (for Native) or KOP_RET (for Script)
                break;
            }
            
            case KOP_GET_GLOBAL: {
                uint8_t rd = READ_REG_IDX();
                uint16_t id = READ_IMM16();
                
                if (id >= vm->chunk->string_count) RUNTIME_ERROR("Global name index out of bounds");
                char* key = vm->chunk->string_table[id];
                
                KValue val;
                if (table_get(&vm->globals, key, &val)) {
                    REG(rd) = val;
                } else {
                    printf("Undefined global: %s\n", key);
                    RUNTIME_ERROR("Undefined global variable");
                }
                break;
            }

            case KOP_SET_GLOBAL: {
                uint8_t ra = READ_REG_IDX();
                uint16_t id = READ_IMM16();
                char* key = vm->chunk->string_table[id];
                // printf("[DEBUG] SET_GLOBAL: %s\n", key);
                table_set(&vm->globals, key, REG(ra));
                break;
            }
            
            case KOP_LDN: {
                uint8_t rd = READ_REG_IDX();
                READ_BYTE(); READ_BYTE(); // Padding
                REG(rd).type = VAL_NULL;
                break;
            }

            case KOP_INSTANCEOF: {
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX(); // Object
                uint8_t rb = READ_REG_IDX(); // Class
                
                bool result = false;
                if (REG(ra).type == VAL_OBJ && REG(rb).type == VAL_OBJ) {
                    KObj* obj = (KObj*)REG(ra).as.obj;
                    KObj* cls_obj = (KObj*)REG(rb).as.obj;
                    
                    if (obj->header.type == OBJ_CLASS_INSTANCE && cls_obj->header.type == OBJ_CLASS) {
                        KObjClass* target = (KObjClass*)cls_obj;
                        KObjClass* curr = ((KObjInstance*)obj)->klass;
                        while (curr) {
                            if (curr == target) {
                                result = true;
                                break;
                            }
                            curr = curr->parent;
                        }
                    }
                }
                REG(rd).type = VAL_BOOL;
                REG(rd).as.boolean = result;
                break;
            }

            case KOP_TRY: {
                READ_BYTE(); // Skip unused register byte
                uint16_t offset = READ_SHORT();
                if (vm->exception_frame_count < KVM_MAX_FRAMES) {
                    ExceptionFrame* frame = &vm->exception_frames[vm->exception_frame_count++];
                    frame->handler_ip = vm->ip + offset; 
                    frame->stack_depth = (int)(vm->stack_top - vm->stack);
                    frame->frame_depth = vm->frame_count;
                } else {
                    printf("Stack Overflow: too many try blocks\n");
                    return 1;
                }
                break;
            }
            case KOP_ENDTRY: {
                if (vm->exception_frame_count > 0) {
                    vm->exception_frame_count--;
                }
                break;
            }
            case KOP_THROW: {
                uint8_t reg = READ_REG_IDX();
                vm->current_exception = REG(reg);
                if (!propagate_exception(vm)) {
                     printf("Unhandled Exception: ");
                     kvm_print_value(vm->current_exception);
                     printf("\n");
                     return 1;
                }
                break;
            }
            case KOP_GETEXCEPTION: {
                uint8_t reg = READ_REG_IDX();
                REG(reg) = vm->current_exception;
                break;
            }

            case KOP_CALL: {
                uint32_t addr = READ_IMM24();
                if (vm->frame_count >= KVM_MAX_FRAMES) RUNTIME_ERROR("Stack overflow (recursion)");
                CallFrame* frame = &vm->frames[vm->frame_count++];
                frame->chunk = vm->chunk;
                frame->ip = vm->ip;
                frame->base_registers = vm->registers;
                frame->return_reg = -1; // No return register
                vm->ip = vm->chunk->code + addr;
                break;
            }
            
            case KOP_FUNCTION: {
                uint16_t name_id = READ_IMM16();
                uint32_t entry = READ_IMM24();
                uint8_t arity = READ_BYTE();
                uint8_t access = READ_BYTE();
                
                char* name = vm->chunk->string_table[name_id];
                
                KObjFunction* func = (KObjFunction*)malloc(sizeof(KObjFunction));
                func->header.type = OBJ_FUNCTION;
                func->header.marked = false;
                func->header.next = vm->objects;
                func->header.size = sizeof(KObjFunction);
                vm->objects = (KObjHeader*)func;
                
                func->name = strdup(name);
                func->arity = arity;
                func->chunk = vm->chunk; 
                func->entry_point = entry;
                func->access = access;
                func->parent_class = NULL;
                func->module = vm->current_module;
                
                KValue val;
                val.type = VAL_OBJ;
                val.as.obj = func;
                
                // table_set(&vm->globals, name, val); // Don't auto-bind
                kvm_push(vm, val);
                break;
            }

            case KOP_RET: {
                READ_IMM24(); // Padding
                
                KValue result = REG(0); // Result in Reg 0 by convention
                
                if (vm->frame_count == 0) {
                    return 0;
                }
                
                vm->frame_count--;
                if (vm->frame_count == 0) {
                    // Top-level return (e.g. thread entry or main)
                    return 0;
                }
                
                CallFrame* frame = &vm->frames[vm->frame_count];
                vm->chunk = frame->chunk;
                vm->ip = frame->ip;
                
                // Restore module context
                if (frame->module) {
                    vm->current_module = frame->module;
                    vm->globals = frame->module->fields;
                } else {
                    // Fallback to initial globals? 
                    // Usually frame->module should be NULL only for top-level script call or similar
                    vm->current_module = NULL;
                    // But wait, if we are returning to top-level, we need to restore its globals.
                    // Top level execution doesn't save its globals in a frame usually?
                    // Ah, when we start interpretation, we don't push a frame.
                    // So if we return from the first called function, we go back to top level.
                    // But top level globals are what?
                    // We need to ensure we don't lose the main module globals.
                    // In kvm_interpret, vm->globals is set.
                    
                    // Actually, if frame->module is NULL, it means we were in the root context (or the frame didn't save it).
                    // If we use vm->globals directly for root, we might need a way to restore it.
                    // But wait, the root context globals ARE vm->globals when we start.
                    // So if we switch globals on CALL, we must restore them on RET.
                    
                    // The issue is: where do we store the "original" globals if frame->module is NULL?
                    // If frame->module is NULL, it implies the caller didn't belong to a module object?
                    // Or maybe it was the main script.
                    // Main script might not have a KObjInstance wrapper if it's just running a file.
                    // But wait, load_module_file creates a KObjInstance for the module.
                    
                    // Let's look at korelin.c again.
                    // When running a module, it sets vm->globals = module_globals.
                    // So the "main" script has its own globals table.
                    // But it doesn't set vm->current_module initially?
                    // We should probably set vm->current_module in korelin.c too.
                }
                
                // Save return reg index from frame
                int return_reg = frame->return_reg;
                
                // Restore registers
                vm->stack_top = vm->registers; // Pop locals/args
                vm->registers = frame->base_registers;
                
                // Write result to caller's register if valid
                if (return_reg != -1) {
                     REG(return_reg) = result;
                }
                break;
            }

            // --- 2.5 對象與字符串 ---
            case KOP_LDC: // Fallthrough for string constants
            case KOP_LDS: { // LDS Rd, Index16
                uint8_t rd = READ_REG_IDX();
                uint16_t index = READ_IMM16();
                
                if (index < vm->chunk->string_count) {
                    REG(rd).type = VAL_STRING;
                    REG(rd).as.str = vm->chunk->string_table[index];
                } else {
                    RUNTIME_ERROR("String constant index out of bounds");
                }
                break;
            }

            case KOP_LDCD: { // LDCD Rd, Imm64
                uint8_t rd = READ_REG_IDX();
                
                uint64_t bits = 0;
                for(int i=0; i<8; i++) {
                     bits = (bits << 8) | READ_BYTE();
                }
                
                REG(rd).type = VAL_DOUBLE;
                union { uint64_t i; double d; } u;
                u.i = bits;
                REG(rd).as.double_prec = u.d;
                break;
            }
            
            case KOP_NEW: { // NEW Rd, TypeId(16), ArgCount(8)
                uint8_t rd = READ_REG_IDX();
                uint16_t type_id = READ_IMM16();
                uint8_t arg_count = READ_BYTE();
                
                char* type_name = vm->chunk->string_table[type_id];
                KValue target_val;
                
                if (!resolve_dotted_name(vm, type_name, &target_val)) {
                     printf("Runtime Error: Undefined type or function '%s'\n", type_name);
                     vm->had_error = true;
                     return 1;
                }
                
                if (target_val.type == VAL_OBJ && ((KObj*)target_val.as.obj)->header.type == OBJ_CLASS) {
                    // Class Instantiation
                    KObjClass* klass = (KObjClass*)target_val.as.obj;
                    
                    KObjInstance* inst = (KObjInstance*)malloc(sizeof(KObjInstance));
                    inst->header.type = OBJ_CLASS_INSTANCE;
                    inst->header.marked = false;
                    inst->header.next = vm->objects;
                    inst->header.size = sizeof(KObjInstance);
                    vm->objects = (KObjHeader*)inst;
                    
                    init_table(&inst->fields);
                    inst->klass = klass;

                    // Set Rd to instance
                    REG(rd).type = VAL_OBJ;
                    REG(rd).as.obj = inst;
                    
                    // Call _init
                    // Look up _init in class hierarchy
                    KValue init_val;
                    bool init_found = false;
                    KObjClass* curr = klass;
                    while (curr) {
                        if (table_get(&curr->methods, "_init", &init_val)) {
                            init_found = true;
                            break;
                        }
                        curr = curr->parent;
                    }
                    
                    if (init_found) {
                        // Pass self (inst) + args
                        // Args are already on stack (pushed by compiler)
                        // We need to insert 'self' before args?
                        // No, compiler pushes args.
                        // We need to construct call properly.
                        
                        // Wait, KOP_CALLR usually expects args on stack.
                        // If we use call_value, we need to arrange stack.
                        // For class method, we need to pass 'self'.
                        // But args are already on stack [arg1, arg2...]
                        // We need [self, arg1, arg2...]
                        
                        // Shift args up by 1 slot to make room for self
                        if (vm->stack_top + 1 - vm->stack >= KVM_STACK_SIZE) RUNTIME_ERROR("Stack overflow");
                        
                        // Move args: src=stack_top-arg_count, dest=src+1, len=arg_count
                        KValue* args_start = vm->stack_top - arg_count;
                        memmove(args_start + 1, args_start, arg_count * sizeof(KValue));
                        
                        *args_start = REG(rd); // self
                        vm->stack_top++; // Stack grew by 1
                        
                        // Call _init, return_reg = -1 (ignore result)
                        if (!call_value(vm, init_val, arg_count + 1, -1)) {
                             return 1;
                        }
                    } else {
                        // No _init, pop args
                        vm->stack_top -= arg_count;
                    }
                } else if (target_val.type == VAL_OBJ && 
                          (((KObj*)target_val.as.obj)->header.type == OBJ_FUNCTION || 
                           ((KObj*)target_val.as.obj)->header.type == OBJ_NATIVE)) {
                    // Factory Function Call
                    // Call function, return_reg = rd
                    if (!call_value(vm, target_val, arg_count, rd)) {
                        return 1;
                    }
                } else {
                    printf("Runtime Error: '%s' is not a class or function\n", type_name);
                    vm->had_error = true;
                    return 1;
                }
                break;
            }

            case KOP_NEWA: { // NEWA Rd, SizeReg
                uint8_t rd = READ_REG_IDX();
                uint8_t rs = READ_REG_IDX();
                READ_BYTE(); // padding
                
                if (REG(rs).type != VAL_INT) RUNTIME_ERROR("Array size must be integer");
                int size = (int)REG_AS_INT(rs);
                if (size < 0) RUNTIME_ERROR("Negative array size");
                
                KObjArray* arr = (KObjArray*)malloc(sizeof(KObjArray));
                if (!arr) RUNTIME_ERROR("Memory allocation failed");
                
                // Init header
                arr->header.type = OBJ_ARRAY;
                arr->header.marked = false;
                arr->header.next = NULL;
                arr->header.size = sizeof(KObjArray) + size * sizeof(KValue);
                
                arr->length = size;
                arr->elements = (KValue*)calloc(size, sizeof(KValue));
                if (!arr->elements && size > 0) {
                    free(arr);
                    RUNTIME_ERROR("Memory allocation failed");
                }
                
                REG(rd).type = VAL_OBJ;
                REG(rd).as.obj = (void*)arr;
                break;
            }

            case KOP_GETFA: { // GETFA Rd, ArrayReg, IndexReg
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX();
                uint8_t rb = READ_REG_IDX();
                
                if (REG(ra).type != VAL_OBJ) RUNTIME_ERROR("Expected array");
                KObjArray* arr = (KObjArray*)REG(ra).as.obj;
                if (!arr || arr->header.type != OBJ_ARRAY) RUNTIME_ERROR("Expected array object");
                
                if (REG(rb).type != VAL_INT) RUNTIME_ERROR("Index must be integer");
                int index = (int)REG_AS_INT(rb);
                if (index < 0 || index >= arr->length) RUNTIME_ERROR("Index out of bounds");
                
                REG(rd) = arr->elements[index];
                break;
            }

            case KOP_PUTFA: { // PUTFA ArrayReg, IndexReg, ValReg
                uint8_t ra = READ_REG_IDX();
                uint8_t rb = READ_REG_IDX();
                uint8_t rc = READ_REG_IDX();
                
                if (REG(ra).type != VAL_OBJ) RUNTIME_ERROR("Expected array");
                KObjArray* arr = (KObjArray*)REG(ra).as.obj;
                if (!arr || arr->header.type != OBJ_ARRAY) RUNTIME_ERROR("Expected array object");
                
                if (REG(rb).type != VAL_INT) RUNTIME_ERROR("Index must be integer");
                int index = (int)REG_AS_INT(rb);
                if (index < 0 || index >= arr->length) RUNTIME_ERROR("Index out of bounds");
                
                arr->elements[index] = REG(rc);
                break;
            }
            
            case KOP_ARRAYLEN: {
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX();
                READ_BYTE(); // padding
                
                if (REG(ra).type != VAL_OBJ) RUNTIME_ERROR("Expected array");
                KObjArray* arr = (KObjArray*)REG(ra).as.obj;
                if (!arr || arr->header.type != OBJ_ARRAY) RUNTIME_ERROR("Expected array object");
                
                REG(rd).type = VAL_INT;
                REG(rd).as.integer = arr->length;
                break;
            }

            case KOP_GETF: { // GETF Rd, Ra, Offset/Id
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX(); // Object
                uint16_t id = READ_IMM16(); // String ID
                
                if (REG(ra).type != VAL_OBJ) {
                    RUNTIME_ERROR("GETF target must be object");
                }
                
                char* key = vm->chunk->string_table[id];
                KObj* obj = (KObj*)REG(ra).as.obj;
                
                if (obj->header.type == OBJ_CLASS_INSTANCE) {
                    KObjInstance* inst = (KObjInstance*)obj;
                    KValue val;
                    printf("DEBUG: GETF %s on Instance\n", key);
                    
                    if (table_get(&inst->fields, key, &val)) {
                        printf("DEBUG: Found in fields\n");
                        REG(rd) = val;
                    } else {
                        // Look up method in class chain
                        bool found = false;
                        KObjClass* curr = inst->klass;
                        while (curr) {
                            if (table_get(&curr->methods, key, &val)) {
                                found = true;
                                break;
                            }
                            curr = curr->parent;
                        }
                        
                        if (found) {
                             printf("DEBUG: Found in methods, creating BoundMethod\n");
                             // Create Bound Method
                             KObjBoundMethod* bound = (KObjBoundMethod*)malloc(sizeof(KObjBoundMethod));
                             bound->header.type = OBJ_BOUND_METHOD;
                             bound->header.marked = false;
                             bound->header.next = vm->objects;
                             bound->header.size = sizeof(KObjBoundMethod);
                             vm->objects = (KObjHeader*)bound;
                             
                             bound->receiver = REG(ra);
                             bound->method = (KObjFunction*)val.as.obj;
                             
                             KValue res;
                             res.type = VAL_OBJ;
                             res.as.obj = bound;
                             REG(rd) = res;
                        } else {
                            // Lazy loading for package submodules
                            if (table_get(&inst->fields, "__name__", &val) && val.type == VAL_STRING) {
                                char full_name[256];
                                snprintf(full_name, sizeof(full_name), "%s.%s", val.as.str, key);
                                
                                // Call import handler
                                if (vm->import_handler) {
                                    KValue submod = vm->import_handler(vm, full_name);
                                    if (submod.type != VAL_NULL) {
                                        // Cache it
                                        table_set(&inst->fields, key, submod);
                                        REG(rd) = submod;
                                        break;
                                    }
                                }
                            }
                            
                            printf("Undefined field: %s\n", key);
                            RUNTIME_ERROR("Undefined field");
                        }
                    }
                } else if (obj->header.type == OBJ_CLASS) {
                    KObjClass* klass = (KObjClass*)obj;
                    KValue val;
                    // Look up static method in chain
                    bool found = false;
                    KObjClass* curr = klass;
                    while (curr) {
                        if (table_get(&curr->methods, key, &val)) {
                            found = true;
                            break;
                        }
                        curr = curr->parent;
                    }
                    
                    if (found) {
                        REG(rd) = val;
                    } else {
                        printf("Undefined static member: %s\n", key);
                        RUNTIME_ERROR("Undefined static member");
                    }
                } else if (obj->header.type == OBJ_ARRAY) {
                    if (strcmp(key, "length") == 0) {
                        KObjArray* arr = (KObjArray*)obj;
                        REG(rd).type = VAL_INT;
                        REG(rd).as.integer = arr->length;
                    } else {
                        RUNTIME_ERROR("Arrays only have 'length' property");
                    }
                } else {
                     RUNTIME_ERROR("GETF not supported on this type");
                }
                break;
            }
            
            case KOP_PUTF: { // PUTF Ra, Rb, Offset/Id (Ra.field = Rb)
                uint8_t ra = READ_REG_IDX(); // Object
                uint8_t rb = READ_REG_IDX(); // Value
                uint16_t id = READ_IMM16(); // String ID
                
                if (REG(ra).type != VAL_OBJ) {
                    RUNTIME_ERROR("PUTF target must be object");
                }
                
                char* key = vm->chunk->string_table[id];
                KObj* obj = (KObj*)REG(ra).as.obj;
                
                if (obj->header.type == OBJ_CLASS_INSTANCE) {
                    KObjInstance* inst = (KObjInstance*)obj;
                    table_set(&inst->fields, key, REG(rb));
                } else {
                    RUNTIME_ERROR("PUTF not supported on this type");
                }
                break;
            }

            case KOP_CLASS: {
                READ_BYTE(); // Padding
                uint16_t name_id = READ_IMM16();
                char* name = vm->chunk->string_table[name_id];
                
                KObjClass* klass = (KObjClass*)malloc(sizeof(KObjClass));
                klass->header.type = OBJ_CLASS;
                klass->header.marked = false;
                klass->header.next = vm->objects;
                klass->header.size = sizeof(KObjClass);
                vm->objects = (KObjHeader*)klass;
                
                klass->name = strdup(name);
                klass->parent = NULL;
                init_table(&klass->methods);
                
                KValue val;
                val.type = VAL_OBJ;
                val.as.obj = klass;
                
                table_set(&vm->globals, name, val);
                break;
            }

            case KOP_METHOD: {
                uint16_t class_name_id = READ_IMM16();
                uint16_t method_name_id = READ_IMM16();
                
                char* class_name = vm->chunk->string_table[class_name_id];
                char* method_name = vm->chunk->string_table[method_name_id];
                
                KValue class_val;
                if (!table_get(&vm->globals, class_name, &class_val)) {
                    RUNTIME_ERROR("Class not defined for method");
                }
                KObjClass* klass = (KObjClass*)class_val.as.obj;
                
                KValue func_val = kvm_pop(vm);
                if (func_val.type != VAL_OBJ || ((KObj*)func_val.as.obj)->header.type != OBJ_FUNCTION) {
                     RUNTIME_ERROR("Method body must be a function");
                }
                
                ((KObjFunction*)func_val.as.obj)->parent_class = klass;
                
                table_set(&klass->methods, method_name, func_val);
                break;
            }

            case KOP_INHERIT: {
                uint16_t sub_name_id = READ_IMM16();
                uint16_t super_name_id = READ_IMM16();
                
                char* sub_name = vm->chunk->string_table[sub_name_id];
                char* super_name = vm->chunk->string_table[super_name_id];
                
                KValue sub_val, super_val;
                if (!table_get(&vm->globals, sub_name, &sub_val)) {
                    RUNTIME_ERROR("Subclass not defined");
                }
                if (!table_get(&vm->globals, super_name, &super_val)) {
                    RUNTIME_ERROR("Superclass not defined");
                }
                
                KObjClass* sub = (KObjClass*)sub_val.as.obj;
                KObjClass* super = (KObjClass*)super_val.as.obj;
                
                sub->parent = super;
                break;
            }

            case KOP_GETSUPER: { // GETSUPER Rd, SelfReg, MethodIdx, ClassIdx
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX();
                uint16_t method_id = READ_IMM16();
                uint16_t class_id = READ_IMM16();
                
                if (REG(ra).type != VAL_OBJ) RUNTIME_ERROR("GETSUPER target must be object");
                
                char* method_name = vm->chunk->string_table[method_id];
                char* class_name = vm->chunk->string_table[class_id];
                
                // Find current class
                KValue class_val;
                if (!table_get(&vm->globals, class_name, &class_val)) {
                    RUNTIME_ERROR("Current class not found for super");
                }
                KObjClass* current_class = (KObjClass*)class_val.as.obj;
                
                // Get Superclass
                KObjClass* super_class = current_class->parent;
                if (!super_class) {
                     RUNTIME_ERROR("Class has no superclass");
                }
                
                // Look up method in superclass chain
                KValue method_val;
                KObjClass* curr = super_class;
                bool found = false;
                while (curr) {
                    if (table_get(&curr->methods, method_name, &method_val)) {
                        found = true;
                        break;
                    }
                    curr = curr->parent;
                }
                
                if (!found) {
                     printf("Method '%s' not found in superclass\n", method_name);
                     RUNTIME_ERROR("Super method not found");
                }
                
                // Create Bound Method
                KObjBoundMethod* bound = (KObjBoundMethod*)malloc(sizeof(KObjBoundMethod));
                bound->header.type = OBJ_BOUND_METHOD;
                bound->header.marked = false;
                bound->header.next = vm->objects;
                bound->header.size = sizeof(KObjBoundMethod);
                vm->objects = (KObjHeader*)bound;
                
                bound->receiver = REG(ra);
                bound->method = (KObjFunction*)method_val.as.obj;
                
                KValue result;
                result.type = VAL_OBJ;
                result.as.obj = bound;
                REG(rd) = result;
                break;
            }
                
            case KOP_INVOKE: {
                uint8_t rd = READ_REG_IDX();
                uint8_t ra = READ_REG_IDX(); // Object Reg
                uint16_t method_id = READ_IMM16();
                uint8_t arg_count = READ_BYTE();
                
                if (REG(ra).type != VAL_OBJ) RUNTIME_ERROR("INVOKE target must be object");
                KObj* obj = (KObj*)REG(ra).as.obj;
                char* method_name = vm->chunk->string_table[method_id];
                
                KValue func_val;
                bool found = false;
                
                // Lookup method/function
                if (obj->header.type == OBJ_CLASS_INSTANCE) {
                    KObjInstance* inst = (KObjInstance*)obj;
                    if (table_get(&inst->fields, method_name, &func_val)) {
                        found = true;
                    } else {
                        // Method chain
                        KObjClass* curr = inst->klass;
                        while (curr) {
                            if (table_get(&curr->methods, method_name, &func_val)) {
                                found = true;
                                break;
                            }
                            curr = curr->parent;
                        }
                    }
                    
                    if (!found) {
                        // Lazy load submodule for packages
                        if (table_get(&inst->fields, "__name__", &func_val) && func_val.type == VAL_STRING) {
                             char full_name[256];
                             snprintf(full_name, sizeof(full_name), "%s.%s", func_val.as.str, method_name);
                             if (vm->import_handler) {
                                 KValue submod = vm->import_handler(vm, full_name);
                                 if (submod.type != VAL_NULL) {
                                     table_set(&inst->fields, method_name, submod);
                                     func_val = submod; 
                                     found = true;
                                 }
                             }
                        }
                    }
                } else if (obj->header.type == OBJ_CLASS) {
                    KObjClass* klass = (KObjClass*)obj;
                    // Static methods chain
                    KObjClass* curr = klass;
                    while (curr) {
                        if (table_get(&curr->methods, method_name, &func_val)) {
                            found = true;
                            break;
                        }
                        curr = curr->parent;
                    }
                }
                
                if (!found) {
                    printf("Undefined method/field: %s\n", method_name);
                    RUNTIME_ERROR("Undefined method");
                }
                
                int effective_arg_count = arg_count;
                bool pass_self = false;
                
                if (func_val.type == VAL_OBJ) {
                    KObj* func_obj = (KObj*)func_val.as.obj;
                    if (func_obj->header.type == OBJ_FUNCTION) {
                        int arity = ((KObjFunction*)func_obj)->arity;
                        if (obj->header.type == OBJ_CLASS_INSTANCE) {
                             if (arity == arg_count + 1) pass_self = true;
                             else pass_self = false; 
                        } else {
                             pass_self = false;
                        }
                    } else if (func_obj->header.type == OBJ_NATIVE) {
                        // Native method on instance: always pass self
                        if (obj->header.type == OBJ_CLASS_INSTANCE) {
                            pass_self = true;
                        }
                    }
                }
                
                // Adjust stack for call
                if (pass_self) {
                     if (vm->stack_top + 1 - vm->stack >= KVM_STACK_SIZE) {
                         printf("Stack overflow\n");
                         return false;
                     }
                     vm->stack_top++;
                     for(int i=0; i<arg_count; i++) {
                         *(vm->stack_top - 1 - i) = *(vm->stack_top - 2 - i);
                     }
                     *(vm->stack_top - arg_count - 1) = REG(ra);
                     effective_arg_count = arg_count + 1;
                }
                
                if (!call_value(vm, func_val, effective_arg_count, rd)) {
                     printf("Call failed\n");
                     vm->had_error = true;
                     return false;
                }
                break;
            }

            // case KOP_INVOKE: // ...

            // --- 2.6 模塊 ---
            case KOP_IMPORT: {
                uint8_t rd = READ_REG_IDX();
                uint16_t name_idx = READ_IMM16();
                
                if (name_idx >= vm->chunk->string_count) {
                    RUNTIME_ERROR("String constant index out of bounds");
                }
                char* name = vm->chunk->string_table[name_idx];
                // printf("[DEBUG] IMPORT: %s\n", name);
                
                KValue val;
                if (table_get(&vm->modules, name, &val)) {
                    REG(rd) = val;
                } else {
                    // Try dynamic import
                    if (vm->import_handler) {
                        val = vm->import_handler(vm, name);
                        if (val.type != VAL_NULL) {
                            table_set(&vm->modules, name, val);
                            REG(rd) = val;
                        } else {
                            printf("Module not found: %s\n", name);
                            RUNTIME_ERROR("Module not found");
                        }
                    } else {
                        printf("Module not found (no loader): %s\n", name);
                        RUNTIME_ERROR("Module not found");
                    }
                }
                break;
            }

            case KOP_SYSCALL: {
                uint8_t id = READ_BYTE();
                READ_BYTE(); READ_BYTE(); // Padding
                
                switch (id) {
                    case 0: { // print(val)
                        if (vm->stack_top <= vm->stack) RUNTIME_ERROR("Stack underflow for syscall print");
                        KValue val = *(--vm->stack_top);
                        kvm_print_value(val);
                        printf("\n");
                        break;
                    }
                    default:
                        RUNTIME_ERROR("Unknown syscall ID");
                }
                break;
            }

            // --- 系統 ---
            case KOP_HALT: {
                READ_IMM24(); // Padding
                return 0;
            }
            
            case KOP_DEBUG: {
                uint8_t rd = READ_REG_IDX();
                READ_BYTE(); READ_BYTE();
                printf("DEBUG: Reg[%d] = ", rd);
                kvm_print_value(REG(rd));
                printf("\n");
                break;
            }

            default: {
                printf("Unknown opcode: 0x%02X\n", opcode);
                RUNTIME_ERROR("Unknown or unimplemented opcode");
            }
        }
    }
    
    return 0;
}

int kvm_interpret(KVM* vm, KBytecodeChunk* chunk) {
    vm->chunk = chunk;
    vm->ip = chunk->code;

    /* JIT Disabled
    // Try JIT Compilation
    if (vm->jit.enabled && !chunk->jit_code) {
        chunk->jit_code = jit_compile(&vm->jit, chunk);
    }

    if (chunk->jit_code) {
        JitFunction func = (JitFunction)chunk->jit_code;
        int res = func(vm);
        return res;
    }
    */

    return kvm_run(vm);
}
