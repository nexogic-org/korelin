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
#include "kstd.h" /** 引入標準庫頭文件 */
#include "kapi.h" /** 引入 KInit */
#include <sys/stat.h>

/**
 * @brief 檢查目錄是否存在
 * @param path 目錄路徑
 * @return 如果存在返回 true，否則返回 false
 */
static bool dir_exists(const char* path) {
    struct stat sb;
    if (stat(path, &sb) == 0 && (sb.st_mode & S_IFDIR)) {
        return true;
    }
    return false;
}

/**
 * @brief 讀取文件內容
 * @param path 文件路徑
 * @param silent 是否靜默失敗
 * @return 文件內容字符串，如果失敗返回 NULL
 */
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

/**
 * @brief 從文件加載模塊
 * @param vm 虛擬機實例
 * @param name 模塊名稱
 * @return 加載的模塊值，如果失敗返回 NULL
 */
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
    char* source = read_file(fullpath, true); /** @brief 靜默探測 */
    if (!source) {
        sprintf(fullpath, "%s.k", path);
        source = read_file(fullpath, true); /** @brief 靜默探測 */
    }
    
    /** @brief 嘗試相對於根目錄 */
    if (!source && vm->root_dir) {
        sprintf(fullpath, "%s/%s.kri", vm->root_dir, path);
        source = read_file(fullpath, true);
        if (!source) {
            sprintf(fullpath, "%s/%s.k", vm->root_dir, path);
            source = read_file(fullpath, true);
        }
    }
    
    if (!source) {
        /** @brief 檢查是否為目錄 */
        if (dir_exists(path)) {
             /** @brief 創建空包模塊 */
             KObjInstance* module = (KObjInstance*)malloc(sizeof(KObjInstance));
             module->header.type = OBJ_CLASS_INSTANCE;
             module->header.marked = false;
             module->header.next = vm->objects;
             module->header.size = sizeof(KObjInstance);
             vm->objects = (KObjHeader*)module;
             module->klass = NULL;
             init_table(&module->fields);
             
             /** @brief 設置 __name__ */
             KValue v_name;
             v_name.type = VAL_STRING;
             
             /** @brief 為名稱分配字符串 */
             KObjString* s_name = (KObjString*)malloc(sizeof(KObjString));
             s_name->header.type = OBJ_STRING;
             s_name->header.marked = false;
             s_name->header.next = vm->objects;
             s_name->header.size = sizeof(KObjString) + strlen(name) + 1;
             vm->objects = (KObjHeader*)s_name;
             s_name->length = strlen(name);
             s_name->chars = strdup(name);
             s_name->hash = 0;
             
             v_name.as.str = s_name->chars; /** @brief 暫時使用原始 char*，因為表鍵/值邏輯可能不同，但讓我們堅持使用標準值 */
             /** @brief 實際上，VAL_STRING 存儲 char*。 */
             
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
    
    /** Copy back fields since vm->globals might have resized (realloc) */
    module->fields = vm->globals;
    
    /** Check for main function in module */
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
    
    /** Set __name__ */
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

/**
 * @brief 模塊導入處理器
 * @param vm 虛擬機實例
 * @param name 模塊名稱
 * @return 導入的模塊值
 */
KValue import_module_handler(KVM* vm, const char* name) {
    /** @brief 0. 首先檢查內部模塊 */
    KValue mod_val;
    if (table_get(&vm->modules, name, &mod_val)) {
        return mod_val;
    }

    /** @brief 1. 嘗試文件加載（直接匹配） */
    KValue val = load_module_file(vm, name);
    if (val.type != VAL_NULL) return val;
    
    /** @brief 2. 嘗試成員訪問（遞歸） */
    const char* last_dot = strrchr(name, '.');
    if (last_dot) {
        int len = last_dot - name;
        if (len >= 1024) len = 1023;
        
        char parent[1024];
        strncpy(parent, name, len);
        parent[len] = '\0';
        
        KValue parent_val;
        /** @brief 檢查父級是否已加載（包括標準庫） */
        if (!table_get(&vm->modules, parent, &parent_val)) {
            parent_val = import_module_handler(vm, parent); /** @brief 遞歸加載 */
        }
        
        if (parent_val.type == VAL_OBJ && parent_val.as.obj != NULL) {
             /** @brief 提取成員 */
             const char* member = last_dot + 1;
             /** @brief 檢查父級是否為實例（模塊） */
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

/**
 * @brief 運行文件
 * @param path 文件路徑
 * @param compile_only 是否僅編譯
 */
static void run_file(const char* path, bool compile_only) {
    /** 檢查文件後綴 */
    const char* ext = strrchr(path, '.');
    if (ext == NULL || (strcmp(ext, ".k") != 0 && strcmp(ext, ".kri") != 0)) {
        printf("Error: File extension must be .k or .kri\n");
        return;
    }

    char* source = read_file(path, false); /** @brief 報告錯誤 */
    if (source == NULL) return;

    /** @brief 1. 詞法分析 */
    /** printf("Initializing Lexer...\n"); */
    Lexer lexer;
    init_lexer(&lexer, source);
    
    /** @brief 2. 語法分析 */
    /** printf("Initializing Parser...\n"); */
    Parser parser;
    init_parser(&parser, &lexer);
    /** printf("Parsing Program...\n"); */
    KastProgram* program = parse_program(&parser);
    if (!program) {
        printf("Parsing failed.\n");
        return;
    }
    /** printf("Compiling Program...\n"); */
    
    if (parser.has_error) {
        printf("Parsing failed.\n");
        free(source);
        return;
    }

    /** @brief 3. 編譯 */
    KBytecodeChunk chunk;
    init_chunk(&chunk);
    
    if (compile_ast(program, &chunk) != 0) {
        printf("Compilation failed.\n");
        free_chunk(&chunk);
        free(source);
        return;
    }
    
    /** 保存字節碼緩存 (如果需要) */
    /** kcache_save("out.kc", &chunk, ...); */

    if (compile_only) {
        printf("Compilation successful.\n");
        /** @brief 釋放資源 */
        free_chunk(&chunk);
        free(source);
        return;
    }

    /** @brief 4. 運行虛擬機 */
    /** printf("Compilation finished.\n"); */
    KVM vm;
    kvm_init(&vm);
    
    /** @brief 設置根目錄 */
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
    
    /** @brief 將虛擬機綁定到 API 並註冊標準庫 */
    KBindVM(&vm);
    kstd_register();
    vm.import_handler = import_module_handler;
    
    /** @brief 啟用 JIT（如果可用） */
    /** ComeOnJIT jit; */
    /** jit_init(&jit); */
    
    /** 嘗試 JIT 編譯 */
    /** void* machine_code = jit_compile(&jit, &chunk); */
    /** if (machine_code) { */
        /** printf("JIT compilation successful. Running native code...\n"); */
        /** ... */
    /** } */
    
    /** 4. Run */
    kvm_interpret(&vm, &chunk);
    
    /** Auto-run main() if it exists */
    if (!vm.had_error) {
        KValue main_func;
        if (table_get(&vm.globals, "main", &main_func) && 
            main_func.type == VAL_OBJ && 
            ((KObj*)main_func.as.obj)->header.type == OBJ_FUNCTION) {
            
            KBytecodeChunk boot_chunk;
            init_chunk(&boot_chunk);
            
            /** fprintf(stderr, "DEBUG: Calling main...\n"); */

            /** Add "main" to string table */
            boot_chunk.string_count = 1;
            boot_chunk.string_table = (char**)malloc(sizeof(char*));
            boot_chunk.string_table[0] = strdup("main");
            
            /** GET_GLOBAL Reg0, "main" (Index 0) */
            write_chunk(&boot_chunk, KOP_GET_GLOBAL, 0);
            write_chunk(&boot_chunk, 0, 0); /** Reg 0 */
            write_chunk(&boot_chunk, 0, 0); /** Index 0 (hi) */
            write_chunk(&boot_chunk, 0, 0); /** Index 0 (lo) */
            
            /** CALLR Reg0, 0 args */
            write_chunk(&boot_chunk, KOP_CALLR, 0);
            write_chunk(&boot_chunk, 0, 0); /** Reg 0 (callee) */
            write_chunk(&boot_chunk, 0, 0); /** Arg count 0 */
            write_chunk(&boot_chunk, 0, 0); /** Padding */
            
            /** RET */
            write_chunk(&boot_chunk, KOP_RET, 0);
            write_chunk(&boot_chunk, 0, 0); 
            write_chunk(&boot_chunk, 0, 0); 
            write_chunk(&boot_chunk, 0, 0); 
            
            kvm_interpret(&vm, &boot_chunk);
            
            free_chunk(&boot_chunk);
        }
    }

    /** jit_cleanup(&jit); */
    kvm_free(&vm);
    free_chunk(&chunk);
    free(source);
}

/** @brief 初始化引擎 */
void kembed_init() {
    /** KInit initializes the core VM structures */
    KInit();
}

/** @brief 清理引擎 */
void kembed_cleanup() {
    KCleanup();
}

/**
 * @brief 執行腳本字符串
 * @param source 腳本源代碼
 */
void kembed_run(const char* source) {
    /** 1. Lexer */
    Lexer lexer;
    init_lexer(&lexer, source);
    
    /** 2. Parser */
    Parser parser;
    init_parser(&parser, &lexer);
    KastProgram* program = parse_program(&parser);
    if (!program || parser.has_error) {
        printf("Parsing failed.\n");
        return;
    }

    /** 3. Compile */
    KBytecodeChunk chunk;
    init_chunk(&chunk);
    
    if (compile_ast(program, &chunk) != 0) {
        printf("Compilation failed.\n");
        free_chunk(&chunk);
        return;
    }
    
    /** @brief 4. 運行虛擬機 */
    KVM vm;
    kvm_init(&vm);
    
    /** @brief 默認根目錄為當前目錄 */
    vm.root_dir = strdup(".");
    
    /** @brief 將虛擬機綁定到 API 並註冊標準庫 */
    KBindVM(&vm);
    kstd_register();
    vm.import_handler = import_module_handler;
    
    /** @brief 運行 */
    kvm_interpret(&vm, &chunk);
    
    /** Auto-run main() if it exists */
    if (!vm.had_error) {
        KValue main_func;
        if (table_get(&vm.globals, "main", &main_func) && 
            main_func.type == VAL_OBJ && 
            ((KObj*)main_func.as.obj)->header.type == OBJ_FUNCTION) {
            
            KBytecodeChunk boot_chunk;
            init_chunk(&boot_chunk);
            
            /** Add "main" to string table */
            boot_chunk.string_count = 1;
            boot_chunk.string_table = (char**)malloc(sizeof(char*));
            boot_chunk.string_table[0] = strdup("main");
            
            /** GET_GLOBAL Reg0, "main" (Index 0) */
            write_chunk(&boot_chunk, KOP_GET_GLOBAL, 0);
            write_chunk(&boot_chunk, 0, 0); /** Reg 0 */
            write_chunk(&boot_chunk, 0, 0); /** Index 0 (hi) */
            write_chunk(&boot_chunk, 0, 0); /** Index 0 (lo) */
            
            /** CALLR Reg0, 0 args */
            write_chunk(&boot_chunk, KOP_CALLR, 0);
            write_chunk(&boot_chunk, 0, 0); /** Reg 0 (callee) */
            write_chunk(&boot_chunk, 0, 0); /** Arg count 0 */
            write_chunk(&boot_chunk, 0, 0); /** Padding */
            
            /** RET */
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

/**
 * @brief 執行腳本文件
 * @param path 文件路徑
 */
void kembed_run_file(const char* path) {
    run_file(path, false);
}
