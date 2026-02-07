#include "kapi.h"
#include "kvm.h"
#include "klex.h"
#include "kparser.h"
#include "kcode.h"
#include "kgc.h"
#include "comeonjit.h"
#include "kstd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- 全局狀態 ---
// Use Thread Local Storage for VM instance to support multi-threading
#if defined(_MSC_VER)
__declspec(thread) KVM* g_current_vm = NULL;
#else
__thread KVM* g_current_vm = NULL;
#endif

static KVM g_internal_vm; // Only used if KInit/KRun manages lifecycle (Main thread only?)
static bool g_initialized = false;


// --- 內部輔助 ---

void KBindVM(KVM* vm) {
    g_current_vm = vm;
}

static void ensure_init() {
    if (!g_initialized && !g_current_vm) {
        kvm_init(&g_internal_vm);
        kstd_register();
        // kgc_init(&g_gc, &g_internal_vm); // Managed inside kvm_init typically?
        // jit_init(&g_jit);
        g_current_vm = &g_internal_vm;
        g_initialized = true;
    }
}

// 註冊本地函數到 VM 全局變量
static void register_native(const char* name, void* func) {
    if (!g_current_vm) return;
    
    // Create Native Object
    KObjNative* native = (KObjNative*)malloc(sizeof(KObjNative));
    native->header.type = OBJ_NATIVE;
    native->header.marked = false;
    native->header.next = g_current_vm->objects;
    native->header.size = sizeof(KObjNative);
    g_current_vm->objects = (KObjHeader*)native;
    
    native->function = (NativeFunc)func;
    native->name = strdup(name);
    
    // Add to globals
    // TODO: Support Modules properly. For now, flat globals or "os.print" as key?
    // KVM usually uses string keys for globals.
    
    KValue val;
    val.type = VAL_OBJ;
    val.as.obj = native;
    
    // Assuming kvm_add_global or similar exists, or directly accessing global table
    // Let's implement a simple global set in kvm or access it here if exposed.
    // kcache.h might have table ops.
    // For now, let's assume we can define globals.
    // But wait, KLibAdd("os", ..., "print") implies "os" object.
    
    // Hack: If name contains '.', try to find parent object?
    // Or just register "os.print" as a global string key?
    // If the parser generates code that looks up "os" then "print", we need an object.
    
    // Simplified: Just register "print" if package is "os"? No, conflict.
    // Register "os_print"?
    // The user wants "os.print".
    
    // Let's check how the Compiler compiles "os.print(...)".
    // It's likely MemberAccess.
    // So "os" must be a global object.
    
    // We need to find or create the "os" object (Struct Instance or similar).
    // Let's create a generic "Module" object (using KObjInstance or new type).
    // Using KObjInstance of an empty class is easiest.
}

// --- API 實現 ---

void KInit() {
    // ensure_init(); 
    // Do nothing if binding external VM
}

// ... KCleanup ...

void KRun(const char* source) {
    ensure_init();
    // ... same as before ...
}

// Global Module Registry (Using VM->modules table)
// typedef struct ModuleEntry { ... } ModuleEntry; // Removed
// static ModuleEntry* g_modules = NULL; // Removed

void KLibNew(const char* package_name) {
    if (!g_current_vm) return;
    
    // Create a new Instance object to represent the module
    KObjInstance* module = (KObjInstance*)malloc(sizeof(KObjInstance));
    module->header.type = OBJ_CLASS_INSTANCE;
    module->header.marked = false;
    module->header.next = g_current_vm->objects;
    module->header.size = sizeof(KObjInstance);
    g_current_vm->objects = (KObjHeader*)module;
    
    module->klass = NULL; // No class definition for raw modules
    // Initialize fields table
    init_table(&module->fields);
    
    // Register as module in VM
    KValue val;
    val.type = VAL_OBJ;
    val.as.obj = module;
    
    // Add to VM modules table
    table_set(&g_current_vm->modules, package_name, val);
    
    // REMOVED: table_set(&g_current_vm->globals, package_name, val);
}

void KLibAdd(const char* package_name, const char* type, const char* name, void* value) {
    if (!g_current_vm) return;
    
    // Find module
    KValue module_val;
    if (!table_get(&g_current_vm->modules, package_name, &module_val)) {
        // Auto-create module if not exists
        KLibNew(package_name);
        table_get(&g_current_vm->modules, package_name, &module_val);
    }
    
    KObjInstance* module = (KObjInstance*)module_val.as.obj;
    
    // Create Native Function Object
    KObjNative* native = (KObjNative*)malloc(sizeof(KObjNative));
    native->header.type = OBJ_NATIVE;
    native->header.marked = false;
    native->header.next = g_current_vm->objects;
    native->header.size = sizeof(KObjNative);
    g_current_vm->objects = (KObjHeader*)native;
    
    native->function = (NativeFunc)value;
    native->name = strdup(name);
    
    KValue val;
    val.type = VAL_OBJ;
    val.as.obj = native;
    
    // Add to module fields
    table_set(&module->fields, name, val);
}

void KLibNewClass(const char* class_name) {
    if (!g_current_vm) return;
    
    KObjClass* klass = (KObjClass*)malloc(sizeof(KObjClass));
    klass->header.type = OBJ_CLASS;
    klass->header.marked = false;
    klass->header.next = g_current_vm->objects;
    klass->header.size = sizeof(KObjClass);
    g_current_vm->objects = (KObjHeader*)klass;
    
    klass->name = strdup(class_name);
    klass->parent = NULL;
    init_table(&klass->methods);
    
    KValue val;
    val.type = VAL_OBJ;
    val.as.obj = klass;
    
    // Register to globals
    table_set(&g_current_vm->globals, class_name, val);
}

void KLibAddMethod(const char* class_name, const char* method_name, void* func) {
    if (!g_current_vm) return;
    
    // Find class in globals
    KValue class_val;
    if (!table_get(&g_current_vm->globals, class_name, &class_val)) {
        // Try to create it if not exists? No, should be created by KLibNewClass
        return;
    }
    
    if (class_val.type != VAL_OBJ || ((KObj*)class_val.as.obj)->header.type != OBJ_CLASS) return;
    KObjClass* klass = (KObjClass*)class_val.as.obj;
    
    // Create Native Function
    KObjNative* native = (KObjNative*)malloc(sizeof(KObjNative));
    native->header.type = OBJ_NATIVE;
    native->header.marked = false;
    native->header.next = g_current_vm->objects;
    native->header.size = sizeof(KObjNative);
    g_current_vm->objects = (KObjHeader*)native;
    
    native->function = (NativeFunc)func;
    native->name = strdup(method_name);
    
    KValue val;
    val.type = VAL_OBJ;
    val.as.obj = native;
    
    table_set(&klass->methods, method_name, val);
}

void KLibAddGlobal(const char* name, void* func) {
    if (!g_current_vm) return;
    
    // Create Native Function
    KObjNative* native = (KObjNative*)malloc(sizeof(KObjNative));
    native->header.type = OBJ_NATIVE;
    native->header.marked = false;
    native->header.next = g_current_vm->objects;
    native->header.size = sizeof(KObjNative);
    g_current_vm->objects = (KObjHeader*)native;
    
    native->function = (NativeFunc)func;
    native->name = strdup(name);
    
    KValue val;
    val.type = VAL_OBJ;
    val.as.obj = native;
    
    table_set(&g_current_vm->globals, name, val);
}

void KLibClassAdd(const char* class_name, const char* member_type, const char* name, void* value, int modifier) {
    if (!g_current_vm) return;
    
    // Find Class Object
    // Assume classes are in globals or modules.
    // For now, search globals (or modules if we knew which module).
    // Let's assume class_name is simple name in globals, or dotted "module.Class"
    
    // Stub implementation for now as we don't have a robust Class registry lookup here yet.
    // In a real implementation, we would:
    // 1. Resolve KObjClass* from class_name
    // 2. Add method/field to class->methods or class->fields
    
    // printf("[DEBUG] KLibClassAdd: %s.%s type=%s mod=%d\n", class_name, name, member_type, modifier);
}

// --- 返回值處理 ---

void KReturnInt(KInt val) {
    // Push return value to stack?
    // Or set a result register?
    // Since KVM calls native via a wrapper or directly, it expects result on stack top?
    // Let's assume KVM expects native func to push result.
    if (!g_current_vm) return;
    
    KValue v;
    v.type = VAL_INT;
    v.as.integer = val;
    
    // *g_current_vm->stack_top = v;
    // g_current_vm->stack_top++;
    kvm_push(g_current_vm, v);
}

void KReturnBool(KBool val) {
    if (!g_current_vm) return;
    KValue v;
    v.type = VAL_BOOL;
    v.as.boolean = val;
    kvm_push(g_current_vm, v);
}

void KReturnVoid() {
    if (!g_current_vm) return;
    KValue v;
    v.type = VAL_NULL;
    kvm_push(g_current_vm, v);
}

void KReturnFloat(KFloat val) {
    if (!g_current_vm) return;
    KValue v;
    v.type = VAL_DOUBLE;
    v.as.double_prec = val;
    kvm_push(g_current_vm, v);
}

void KReturnString(const char* s) {
     if (!g_current_vm) return;
     
     // Create String Object
     KObjString* str = (KObjString*)malloc(sizeof(KObjString));
     str->header.type = OBJ_STRING;
     str->header.marked = false;
     str->header.next = g_current_vm->objects;
     str->header.size = sizeof(KObjString) + strlen(s) + 1;
     g_current_vm->objects = (KObjHeader*)str;
     
     str->length = strlen(s);
     str->chars = strdup(s);
     str->hash = 0;
     
     KValue v;
     v.type = VAL_OBJ;
     v.as.obj = str;
     // printf("[DEBUG] KReturnString: %p chars='%s'\n", str, str->chars);
     kvm_push(g_current_vm, v);
}

// --- 參數獲取 ---

int KGetArgCount() {
    if (!g_current_vm) return 0;
    return g_current_vm->native_argc;
}

KInt KGetArgInt(int index) {
    if (!g_current_vm || !g_current_vm->native_args) return 0;
    if (index < 0 || index >= g_current_vm->native_argc) return 0;
    KValue val = g_current_vm->native_args[index];
    if (val.type == VAL_INT) return val.as.integer;
    if (val.type == VAL_FLOAT) return (KInt)val.as.single_prec; // Auto convert?
    if (val.type == VAL_DOUBLE) return (KInt)val.as.double_prec;
    return 0;
}

KFloat KGetArgFloat(int index) {
    if (!g_current_vm || !g_current_vm->native_args) return 0.0;
    if (index < 0 || index >= g_current_vm->native_argc) return 0.0;
    KValue val = g_current_vm->native_args[index];
    if (val.type == VAL_FLOAT) return (double)val.as.single_prec;
    if (val.type == VAL_DOUBLE) return val.as.double_prec;
    if (val.type == VAL_INT) return (double)val.as.integer;
    return 0.0;
}

KString KGetArgString(int index) {
    if (!g_current_vm || !g_current_vm->native_args) return NULL;
    if (index < 0 || index >= g_current_vm->native_argc) return NULL;
    KValue val = g_current_vm->native_args[index];
    
    if (val.type == VAL_STRING) return val.as.str;
    
    if (val.type == VAL_OBJ) {
        KObj* obj = (KObj*)val.as.obj;
        if (obj->header.type == OBJ_STRING) {
            return ((KObjString*)obj)->chars;
        }
    }
    return NULL;
}

KBool KGetArgBool(int index) {
    if (!g_current_vm || !g_current_vm->native_args) return false;
    if (index < 0 || index >= g_current_vm->native_argc) return false;
    KValue val = g_current_vm->native_args[index];
    if (val.type == VAL_BOOL) return val.as.boolean;
    if (val.type == VAL_INT) return val.as.integer != 0;
    if (val.type == VAL_NULL) return false;
    return true; 
}
