//
// Created by Helix on 2026/1/21.
//
#include "kconst.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "klex.h"
#include "kparser.h"
#include "kcode.h"
#include "kvm.h"
#include "kcache.h"
#include "comeonjit.h"
#include "kgc.h"
#include "kstd.h" // 引入標準庫頭文件
#include "kapi.h" // 引入 KInit
#include <sys/stat.h>

// 檢查目錄是否存在
static bool dir_exists(const char* path) {
    struct stat sb;
    if (stat(path, &sb) == 0 && (sb.st_mode & S_IFDIR)) {
        return true;
    }
    return false;
}

// 讀取文件內容
static char* read_file(const char* path, bool silent) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        if (!silent) fprintf(stderr, "Could not open file \"%s\".\n", path);
        return NULL;
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

// Helper to load module from file
static KValue load_module_file(KVM* vm, const char* name) {
    char path[1024];
    int i=0, j=0;
    while(name[i]) {
        if(name[i] == '.') path[j++] = '/';
        else path[j++] = name[i];
        i++;
    }
    path[j] = '\0';
    
    char fullpath[1024];
    sprintf(fullpath, "%s.kri", path);
    char* source = read_file(fullpath, true); // Silent probe
    if (!source) {
        sprintf(fullpath, "%s.k", path);
        source = read_file(fullpath, true); // Silent probe
    }
    
    // Try relative to root dir
    if (!source && vm->root_dir) {
        sprintf(fullpath, "%s/%s.kri", vm->root_dir, path);
        source = read_file(fullpath, true);
        if (!source) {
            sprintf(fullpath, "%s/%s.k", vm->root_dir, path);
            source = read_file(fullpath, true);
        }
    }
    
    if (!source) {
        // Check if directory
        if (dir_exists(path)) {
             // Create empty package module
             KObjInstance* module = (KObjInstance*)malloc(sizeof(KObjInstance));
             module->header.type = OBJ_CLASS_INSTANCE;
             module->header.marked = false;
             module->header.next = vm->objects;
             module->header.size = sizeof(KObjInstance);
             vm->objects = (KObjHeader*)module;
             module->klass = NULL;
             init_table(&module->fields);
             
             // Set __name__
             KValue v_name;
             v_name.type = VAL_STRING;
             
             // Allocate string for name
             KObjString* s_name = (KObjString*)malloc(sizeof(KObjString));
             s_name->header.type = OBJ_STRING;
             s_name->header.marked = false;
             s_name->header.next = vm->objects;
             s_name->header.size = sizeof(KObjString) + strlen(name) + 1;
             vm->objects = (KObjHeader*)s_name;
             s_name->length = strlen(name);
             s_name->chars = strdup(name);
             s_name->hash = 0;
             
             v_name.as.str = s_name->chars; // Using raw char* for now as table key/val logic might differ, but let's stick to standard value
             // Actually, VAL_STRING stores char*.
             
             table_set(&module->fields, "__name__", v_name);
             
             KValue val;
             val.type = VAL_OBJ;
             val.as.obj = module;
             return val;
        }
        return (KValue){VAL_NULL};
    }
    
    Lexer lexer; init_lexer(&lexer, source);
    Parser parser; init_parser(&parser, &lexer);
    KastProgram* program = parse_program(&parser);
    if (parser.has_error) {
        printf("Error parsing module %s\n", name);
        free(source);
        return (KValue){VAL_NULL};
    }
    
    KBytecodeChunk* chunk = (KBytecodeChunk*)malloc(sizeof(KBytecodeChunk));
    init_chunk(chunk);
    if (compile_ast(program, chunk) != 0) {
        printf("Error compiling module %s\n", name);
        free_chunk(chunk); free(chunk); free(source);
        return (KValue){VAL_NULL};
    }
    
    KBytecodeChunk* saved_chunk = vm->chunk;
    uint8_t* saved_ip = vm->ip;
    KTable saved_globals = vm->globals;
    KObjInstance* saved_module = vm->current_module;
    
    KObjInstance* module = (KObjInstance*)malloc(sizeof(KObjInstance));
    module->header.type = OBJ_CLASS_INSTANCE;
    module->header.marked = false;
    module->header.next = vm->objects;
    module->header.size = sizeof(KObjInstance);
    vm->objects = (KObjHeader*)module;
    module->klass = NULL;
    init_table(&module->fields);

    vm->globals = module->fields;
    vm->current_module = module;
    
    kvm_interpret(vm, chunk);
    
    // Copy back fields since vm->globals might have resized (realloc)
    module->fields = vm->globals;
    
    // Check for main function in module
    KValue main_val;
    if (table_get(&vm->globals, "main", &main_val)) {
        if (main_val.type == VAL_OBJ && ((KObj*)main_val.as.obj)->header.type == OBJ_FUNCTION) {
             printf("Error: Module '%s' cannot define 'main' function.\n", name);
             
             vm->chunk = saved_chunk;
             vm->ip = saved_ip;
             vm->globals = saved_globals;
             vm->current_module = saved_module;
             
             free_chunk(chunk); free(chunk); free(source);
             return (KValue){VAL_NULL};
        }
    }
    
    // Set __name__
    KValue v_name;
    v_name.type = VAL_STRING;
    KObjString* s_name = (KObjString*)malloc(sizeof(KObjString));
    s_name->header.type = OBJ_STRING;
    s_name->header.marked = false;
    s_name->header.next = vm->objects;
    s_name->header.size = sizeof(KObjString) + strlen(name) + 1;
    vm->objects = (KObjHeader*)s_name;
    s_name->length = strlen(name);
    s_name->chars = strdup(name);
    s_name->hash = 0;
    v_name.as.str = s_name->chars;
    table_set(&module->fields, "__name__", v_name);

    vm->chunk = saved_chunk;
    vm->ip = saved_ip;
    vm->globals = saved_globals;
    vm->current_module = saved_module;
    
    free(source);
    
    KValue val;
    val.type = VAL_OBJ;
    val.as.obj = module;
    return val;
}

// 模塊導入處理器
KValue import_module_handler(KVM* vm, const char* name) {
    // 0. Check internal modules first
    KValue mod_val;
    if (table_get(&vm->modules, name, &mod_val)) {
        return mod_val;
    }

    // 1. Try file load (Direct match)
    KValue val = load_module_file(vm, name);
    if (val.type != VAL_NULL) return val;
    
    // 2. Try member access (Recursive)
    const char* last_dot = strrchr(name, '.');
    if (last_dot) {
        char parent[1024];
        int len = last_dot - name;
        strncpy(parent, name, len);
        parent[len] = '\0';
        
        KValue parent_val;
        // Check if parent is already loaded (including standard libs)
        if (!table_get(&vm->modules, parent, &parent_val)) {
            parent_val = import_module_handler(vm, parent); // Recursive load
        }
        
        if (parent_val.type == VAL_OBJ) {
             // Extract member
             const char* member = last_dot + 1;
             // Check if parent is instance (module)
             if (((KObj*)parent_val.as.obj)->header.type == OBJ_CLASS_INSTANCE) {
                 KObjInstance* inst = (KObjInstance*)parent_val.as.obj;
                 KValue field;
                 if (table_get(&inst->fields, member, &field)) {
                     return field;
                 }
             }
        }
    }
    
    return (KValue){VAL_NULL};
}

// 運行文件
static void run_file(const char* path, bool compile_only) {
    // 檢查文件後綴
    const char* ext = strrchr(path, '.');
    if (ext == NULL || (strcmp(ext, ".k") != 0 && strcmp(ext, ".kri") != 0)) {
        printf("Error: File extension must be .k or .kri\n");
        return;
    }

    char* source = read_file(path, false); // Report error
    if (source == NULL) return;

    // 1. Lexer
    // printf("Initializing Lexer...\n");
    Lexer lexer;
    init_lexer(&lexer, source);
    
    // 2. Parser
    // printf("Initializing Parser...\n");
    Parser parser;
    init_parser(&parser, &lexer);
    // printf("Parsing Program...\n");
    KastProgram* program = parse_program(&parser);
    if (!program) {
        printf("Parsing failed.\n");
        return;
    }
    // printf("Compiling Program...\n");
    
    if (parser.has_error) {
        printf("Parsing failed.\n");
        free(source);
        return;
    }

    // 3. Compile
    KBytecodeChunk chunk;
    init_chunk(&chunk);
    
    if (compile_ast(program, &chunk) != 0) {
        printf("Compilation failed.\n");
        free_chunk(&chunk);
        free(source);
        return;
    }
    
    // 保存字節碼緩存 (如果需要)
    // kcache_save("out.kc", &chunk, ...);

    if (compile_only) {
        printf("Compilation successful.\n");
        // free resources
        free_chunk(&chunk);
        free(source);
        return;
    }

    // 4. Run VM
    // printf("Compilation finished.\n");
    KVM vm;
    kvm_init(&vm);
    
    // Set root dir
    char* last_slash = strrchr(path, '/');
    char* last_backslash = strrchr(path, '\\');
    char* slash = last_slash > last_backslash ? last_slash : last_backslash;
    if (slash) {
        size_t len = slash - path;
        vm.root_dir = (char*)malloc(len + 1);
        strncpy(vm.root_dir, path, len);
        vm.root_dir[len] = '\0';
    } else {
        vm.root_dir = strdup(".");
    }
    
    // Bind VM to API and register standard libraries
    KBindVM(&vm);
    kstd_register();
    vm.import_handler = import_module_handler;
    
    // Enable JIT (if available)
    // ComeOnJIT jit;
    // jit_init(&jit);
    
    // 嘗試 JIT 編譯
    // void* machine_code = jit_compile(&jit, &chunk);
    // if (machine_code) {
        // printf("JIT compilation successful. Running native code...\n");
        // ...
    // }
    
    // 4. Run
    kvm_interpret(&vm, &chunk);
    
    // Auto-run main() if it exists
    if (!vm.had_error) {
        KValue main_func;
        if (table_get(&vm.globals, "main", &main_func) && 
            main_func.type == VAL_OBJ && 
            ((KObj*)main_func.as.obj)->header.type == OBJ_FUNCTION) {
            
            KBytecodeChunk boot_chunk;
            init_chunk(&boot_chunk);
            
            // fprintf(stderr, "DEBUG: Calling main...\n");

            // Add "main" to string table
            boot_chunk.string_count = 1;
            boot_chunk.string_table = (char**)malloc(sizeof(char*));
            boot_chunk.string_table[0] = strdup("main");
            
            // GET_GLOBAL Reg0, "main" (Index 0)
            write_chunk(&boot_chunk, KOP_GET_GLOBAL, 0);
            write_chunk(&boot_chunk, 0, 0); // Reg 0
            write_chunk(&boot_chunk, 0, 0); // Index 0 (hi)
            write_chunk(&boot_chunk, 0, 0); // Index 0 (lo)
            
            // CALLR Reg0, 0 args
            write_chunk(&boot_chunk, KOP_CALLR, 0);
            write_chunk(&boot_chunk, 0, 0); // Reg 0 (callee)
            write_chunk(&boot_chunk, 0, 0); // Arg count 0
            write_chunk(&boot_chunk, 0, 0); // Padding
            
            // RET
            write_chunk(&boot_chunk, KOP_RET, 0);
            write_chunk(&boot_chunk, 0, 0); 
            write_chunk(&boot_chunk, 0, 0); 
            write_chunk(&boot_chunk, 0, 0); 
            
            kvm_interpret(&vm, &boot_chunk);
            
            free_chunk(&boot_chunk);
        }
    }

    // jit_cleanup(&jit);
    kvm_free(&vm);
    free_chunk(&chunk);
    free(source);
}

// kembed_init - Initialize the engine
void kembed_init() {
    // KInit initializes the core VM structures
    KInit();
}

// kembed_cleanup - Cleanup the engine
void kembed_cleanup() {
    KCleanup();
}

// kembed_run - Execute a script string
void kembed_run(const char* source) {
    // 1. Lexer
    Lexer lexer;
    init_lexer(&lexer, source);
    
    // 2. Parser
    Parser parser;
    init_parser(&parser, &lexer);
    KastProgram* program = parse_program(&parser);
    if (!program || parser.has_error) {
        printf("Parsing failed.\n");
        return;
    }

    // 3. Compile
    KBytecodeChunk chunk;
    init_chunk(&chunk);
    
    if (compile_ast(program, &chunk) != 0) {
        printf("Compilation failed.\n");
        free_chunk(&chunk);
        return;
    }
    
    // 4. Run VM
    KVM vm;
    kvm_init(&vm);
    
    // Default root dir to current directory
    vm.root_dir = strdup(".");
    
    // Bind VM to API and register standard libraries
    KBindVM(&vm);
    kstd_register();
    vm.import_handler = import_module_handler;
    
    // Run
    kvm_interpret(&vm, &chunk);
    
    // Auto-run main() if it exists
    if (!vm.had_error) {
        KValue main_func;
        if (table_get(&vm.globals, "main", &main_func) && 
            main_func.type == VAL_OBJ && 
            ((KObj*)main_func.as.obj)->header.type == OBJ_FUNCTION) {
            
            KBytecodeChunk boot_chunk;
            init_chunk(&boot_chunk);
            
            // Add "main" to string table
            boot_chunk.string_count = 1;
            boot_chunk.string_table = (char**)malloc(sizeof(char*));
            boot_chunk.string_table[0] = strdup("main");
            
            // GET_GLOBAL Reg0, "main" (Index 0)
            write_chunk(&boot_chunk, KOP_GET_GLOBAL, 0);
            write_chunk(&boot_chunk, 0, 0); // Reg 0
            write_chunk(&boot_chunk, 0, 0); // Index 0 (hi)
            write_chunk(&boot_chunk, 0, 0); // Index 0 (lo)
            
            // CALLR Reg0, 0 args
            write_chunk(&boot_chunk, KOP_CALLR, 0);
            write_chunk(&boot_chunk, 0, 0); // Reg 0 (callee)
            write_chunk(&boot_chunk, 0, 0); // Arg count 0
            write_chunk(&boot_chunk, 0, 0); // Padding
            
            // RET
            write_chunk(&boot_chunk, KOP_RET, 0);
            write_chunk(&boot_chunk, 0, 0); 
            write_chunk(&boot_chunk, 0, 0); 
            write_chunk(&boot_chunk, 0, 0); 
            
            kvm_interpret(&vm, &boot_chunk);
            free_chunk(&boot_chunk);
        }
    }

    kvm_free(&vm);
    free_chunk(&chunk);
}

// kembed_run_file - Execute a script file
void kembed_run_file(const char* path) {
    run_file(path, false);
}
