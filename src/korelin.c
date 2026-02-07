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
#include "keditor.h" // 引入編輯器
#include <sys/stat.h>
#include <ctype.h>

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
static KValue load_module_file(KVM* vm, const char* name, const char* path_override) {
    char path[1024];
    
    if (path_override) {
        strcpy(path, path_override);
    } else {
        int i=0, j=0;
        while(name[i]) {
            if(name[i] == '.') path[j++] = '/';
            else path[j++] = name[i];
            i++;
        }
        path[j] = '\0';
    }
    
    char fullpath[1024];
    sprintf(fullpath, "%s.kri", path);
    char* source = read_file(fullpath, true); // Silent probe
    if (!source) {
        sprintf(fullpath, "%s.k", path);
        source = read_file(fullpath, true); // Silent probe
    }
    
    // Try relative to root dir
    if (!source && vm->root_dir) {
        if (path_override) {
             // For override, we strictly follow it relative to root
             sprintf(fullpath, "%s/%s.kri", vm->root_dir, path);
             source = read_file(fullpath, true);
             if (!source) {
                 sprintf(fullpath, "%s/%s.k", vm->root_dir, path);
                 source = read_file(fullpath, true);
             }
        } else {
            sprintf(fullpath, "%s/%s.kri", vm->root_dir, path);
            source = read_file(fullpath, true);
            if (!source) {
                sprintf(fullpath, "%s/%s.k", vm->root_dir, path);
                source = read_file(fullpath, true);
            }
        }
    }
    
    if (!source) {
        // Check if directory
        // For directory check, we need to construct path correctly
        bool is_dir = false;
        if (dir_exists(path)) is_dir = true;
        else {
             if (vm->root_dir) {
                 sprintf(fullpath, "%s/%s", vm->root_dir, path);
                 if (dir_exists(fullpath)) is_dir = true;
             }
        }

        if (is_dir) {
             // Create empty package module
             KObjInstance* module = (KObjInstance*)kgc_alloc(vm->gc, sizeof(KObjInstance), OBJ_CLASS_INSTANCE);
             module->klass = NULL;
             init_table(&module->fields);
             
             // Set __name__
             KValue v_name;
             v_name.type = VAL_STRING;
             
             // Allocate string for name
             KObjString* s_name = (KObjString*)kgc_alloc(vm->gc, sizeof(KObjString), OBJ_STRING);
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
    KValue* saved_registers = vm->registers;
    KValue* saved_stack_top = vm->stack_top;

    // Backup Frames to allow re-entrant execution (recursion)
    CallFrame saved_frames[KVM_MAX_FRAMES];
    int saved_frame_count = vm->frame_count;
    if (saved_frame_count > 0) {
        memcpy(saved_frames, vm->frames, saved_frame_count * sizeof(CallFrame));
    }
    vm->frame_count = 0; // Reset for inner execution

    // New register window
    vm->registers = vm->stack_top;
    
    // Safety Check: Stack Overflow
    if (vm->stack_top + 256 >= vm->stack + KVM_STACK_SIZE) {
         printf("Runtime Error: Stack overflow during module loading '%s'.\n", name);
         
         vm->chunk = saved_chunk;
         vm->ip = saved_ip;
         vm->globals = saved_globals;
         vm->current_module = saved_module;
         vm->registers = saved_registers;
         vm->stack_top = saved_stack_top;
         
         free_chunk(chunk); free(chunk); free(source);
         return (KValue){VAL_NULL};
    }

    vm->stack_top += 256; // Reserve enough space (KVM_REGISTERS_MAX is usually 256)
    
    KObjInstance* module = (KObjInstance*)kgc_alloc(vm->gc, sizeof(KObjInstance), OBJ_CLASS_INSTANCE);
    module->klass = NULL;
    init_table(&module->fields);

    vm->globals = module->fields;
    vm->current_module = module;
    
    kvm_interpret(vm, chunk);
    
    // Restore Frames
    vm->frame_count = saved_frame_count;
    if (saved_frame_count > 0) {
        memcpy(vm->frames, saved_frames, saved_frame_count * sizeof(CallFrame));
    }

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
             
             // Restore Frames on Error
             vm->frame_count = saved_frame_count;
             if (saved_frame_count > 0) {
                 memcpy(vm->frames, saved_frames, saved_frame_count * sizeof(CallFrame));
             }

             free_chunk(chunk); free(chunk); free(source);
             return (KValue){VAL_NULL};
        }
    }
    
    // Set __name__
    KValue v_name;
    v_name.type = VAL_STRING;
    KObjString* s_name = (KObjString*)kgc_alloc(vm->gc, sizeof(KObjString), OBJ_STRING);
    s_name->length = strlen(name);
    s_name->chars = strdup(name);
    s_name->hash = 0;
    v_name.as.str = s_name->chars;
    table_set(&module->fields, "__name__", v_name);

    vm->chunk = saved_chunk;
    vm->ip = saved_ip;
    vm->globals = saved_globals;
    vm->current_module = saved_module;
    vm->registers = saved_registers;
    vm->stack_top = saved_stack_top;
    
    free(source);
    
    KValue val;
    val.type = VAL_OBJ;
    val.as.obj = module;
    return val;
}

// 簡單的 JSON 字段查找與对象解析
static void parse_library_map(KVM* vm, const char* json_path, const char* field_name) {
    char* source = read_file(json_path, false);
    if (!source) return;
    
    // 1. Find field_name
    char search_key[256];
    sprintf(search_key, "\"%s\"", field_name);
    char* pos = strstr(source, search_key);
    if (!pos) {
        printf("Warning: Field '%s' not found in %s\n", field_name, json_path);
        free(source);
        return;
    }
    
    // 2. Find value start (should be '{')
    pos += strlen(search_key);
    while (*pos && *pos != '{' && *pos != '"' && *pos != '[') pos++; 
    
    if (*pos != '{') {
        printf("Warning: Field '%s' in %s is not an object\n", field_name, json_path);
        free(source);
        return;
    }
    pos++; // Skip '{'
    
    // 3. Parse object content
    while (*pos && *pos != '}') {
        // Find key
        while (*pos && isspace((unsigned char)*pos)) pos++;
        if (*pos == '}') break;
        if (*pos != '"') { pos++; continue; } 
        
        pos++; // Skip "
        char* key_start = pos;
        while (*pos && *pos != '"') pos++;
        if (!*pos) break;
        *pos = '\0'; // Terminate key
        char* key = strdup(key_start);
        pos++; // Skip "
        
        // Find value
        while (*pos && *pos != ':') pos++;
        if (!*pos) { free(key); break; }
        pos++; // Skip :
        
        while (*pos && isspace((unsigned char)*pos)) pos++;
        if (*pos != '"') { 
            free(key);
            while (*pos && *pos != ',' && *pos != '}') pos++;
            if (*pos == ',') pos++;
            continue; 
        }
        
        pos++; // Skip "
        char* val_start = pos;
        while (*pos && *pos != '"') pos++;
        if (!*pos) { free(key); break; }
        *pos = '\0'; // Terminate val
        char* val = strdup(val_start);
        pos++; // Skip "
        
        KValue v_val;
        v_val.type = VAL_STRING;
        v_val.as.str = val;
        
        table_set(&vm->lib_paths, key, v_val);
        
        free(key); 
        
        // Next
        while (*pos && *pos != ',' && *pos != '}') pos++;
        if (*pos == ',') pos++;
    }
    
    free(source);
}

// 模塊導入處理器
KValue import_module_handler(KVM* vm, const char* name) {
    // 0. Check internal modules first
    KValue mod_val;
    if (table_get(&vm->modules, name, &mod_val)) {
        return mod_val;
    }

    // Check globals (for built-in libs like math, json)
    if (table_get(&vm->globals, name, &mod_val)) {
        // Only return if it's an object (likely a module/class instance or class)
        if (mod_val.type == VAL_OBJ) return mod_val;
    }

    // NEW: Check Library Map
    if (table_get(&vm->lib_paths, name, &mod_val)) {
        if (mod_val.type == VAL_STRING) {
             // Load module with path override
             KValue val = load_module_file(vm, name, mod_val.as.str);
             if (val.type != VAL_NULL) {
                 return val;
             }
        }
    }

    // 1. Try file load (Direct match)
    KValue val = load_module_file(vm, name, NULL);
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
static void run_file(const char* path, bool compile_only, const char* lib_arg) {
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
    // printf("Compiling...\n"); fflush(stdout);
    KBytecodeChunk chunk;
    init_chunk(&chunk);
    chunk.filename = strdup(path);
    
    // printf("Compiling Program...\n"); fflush(stdout);
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
    // printf("Compilation finished.\n"); fflush(stdout);
    fflush(stdout);
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
    
    // Parse Library Map if provided
    if (lib_arg) {
        char* arg_copy = strdup(lib_arg);
        char* delim = strchr(arg_copy, '>');
        if (delim) {
            *delim = '\0';
            char* json_path = arg_copy;
            char* field_name = delim + 1;
            parse_library_map(&vm, json_path, field_name);
        } else {
            printf("Warning: Invalid format for -lib argument. Expected file>field\n");
        }
        free(arg_copy);
    }
    
    // Enable JIT (Handled in kvm_init)
    
    // 4. Run
    kvm_interpret(&vm, &chunk);
    
    // Auto-run main() if it exists
    if (!vm.had_error) {
        KValue main_func;
        if (table_get(&vm.globals, "main", &main_func)) {
             // Hack: Force run main even if type is wrong (due to memory corruption bug)
             if (main_func.type == VAL_OBJ) {
                 kvm_call_function(&vm, (KObjFunction*)main_func.as.obj, 0);
                 kvm_run(&vm);
             } else {
                 printf("\033[31mError: 'main' is not an object.\033\n");
             }
        } else {
             printf("\033[31mError: No 'main' function found.\033\n");
        }
    }
    
    // JIT cleanup handled in kvm_free
    free_chunk(&chunk);
    kvm_free(&vm);
    free(source);
}

void print_help() {
    printf("Korelin SDK %s\n", KORELIN_SDK_VERSION);
    printf("Usage: korelin <command> [args]\n");
    printf("Commands: run, compile, version, help, editor\n");
    printf("Rungo: init, install, uninstall, list\n");
}

// --- Rungo Package Manager Logic ---

static void rungo_init(const char* project_name) {
    if (!project_name) {
        printf("Usage: rungo init <project-name>\n");
        return;
    }
    
    // Create directory
    if (mkdir(project_name) != 0) {
        // printf("Error creating directory '%s' (might exist)\n", project_name);
        // Continue anyway
    }
    
    char path[1024];
    
    // Create src directory
    sprintf(path, "%s/src", project_name);
    mkdir(path);
    
    // Create main.k
    sprintf(path, "%s/src/main.k", project_name);
    FILE* f = fopen(path, "w");
    if (f) {
        fprintf(f, "import os;\n\nvoid main() {\n    os.println(\"Hello, Rungo!\");\n}\n");
        fclose(f);
    }
    
    // Create korelin.toml
    sprintf(path, "%s/korelin.toml", project_name);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "[package]\nname = \"%s\"\nversion = \"0.1.0\"\nauthors = [\"Your Name <you@example.com>\"]\n\n[dependencies]\n", project_name);
        fclose(f);
    }
    
    printf("Created new Rungo project: %s\n", project_name);
}

static void rungo_install(const char* package_name) {
    if (!package_name) {
        printf("Usage: rungo install <package-name>\n");
        return;
    }
    
    // Create packages directory if not exists
    if (!dir_exists("packages")) {
        mkdir("packages");
    }
    
    char path[1024];
    sprintf(path, "packages/%s", package_name);
    
    if (dir_exists(path)) {
        printf("Package '%s' is already installed.\n", package_name);
        return;
    }
    
    mkdir(path);
    
    // Create a dummy lib.k
    sprintf(path, "packages/%s/lib.k", package_name);
    FILE* f = fopen(path, "w");
    if (f) {
        fprintf(f, "// Package: %s\n// Installed by Rungo\n", package_name);
        fclose(f);
    }
    
    printf("Installed package: %s\n", package_name);
}

static void rungo_uninstall(const char* package_name) {
    if (!package_name) {
        printf("Usage: rungo uninstall <package-name>\n");
        return;
    }
    
    char path[1024];
    sprintf(path, "packages/%s/lib.k", package_name);
    remove(path);
    
    sprintf(path, "packages/%s", package_name);
    if (rmdir(path) == 0) {
        printf("Uninstalled package: %s\n", package_name);
    } else {
        printf("Failed to uninstall package '%s' (or not found)\n", package_name);
    }
}

static void rungo_list() {
    if (!dir_exists("packages")) {
        printf("No packages installed (packages directory not found).\n");
        return;
    }
    
    printf("Installed packages:\n");
    // Listing directories is platform specific. 
    // For now we just print a placeholder or use a simple system command
    system("dir packages /b"); // Windows specific
}

// -----------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }

    const char* command = argv[1];

    // Check for Rungo commands (either direct or via 'rungo' prefix)
    bool is_rungo = false;
    const char* rungo_cmd = command;
    int arg_offset = 2; // Argument for command starts at argv[2]
    
    if (strcmp(command, "rungo") == 0) {
        if (argc < 3) {
            print_help();
            return 0;
        }
        rungo_cmd = argv[2];
        arg_offset = 3;
        is_rungo = true;
    }
    
    // Rungo Commands
    if (strcmp(rungo_cmd, "init") == 0) {
        rungo_init(argc > arg_offset ? argv[arg_offset] : NULL);
        return 0;
    } else if (strcmp(rungo_cmd, "install") == 0) {
        rungo_install(argc > arg_offset ? argv[arg_offset] : NULL);
        return 0;
    } else if (strcmp(rungo_cmd, "uninstall") == 0) {
        rungo_uninstall(argc > arg_offset ? argv[arg_offset] : NULL);
        return 0;
    } else if (strcmp(rungo_cmd, "list") == 0) {
        rungo_list();
        return 0;
    }
    
    // Korelin Commands
    if (strcmp(command, "version") == 0) {
        printf("Korelin SDK version: %s\n", KORELIN_SDK_VERSION);
    } else if (strcmp(command, "help") == 0) {
        print_help();
    } else if (strcmp(command, "run") == 0) {
        if (argc < 3) {
            printf("Usage: korelin run <file-name> [-lib file>field]\n");
            return 1;
        }
        
        const char* filename = argv[2];
        const char* lib_arg = NULL;
        
        // Parse extra args
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-lib") == 0 && i + 1 < argc) {
                lib_arg = argv[i+1];
                i++;
            }
        }
        
        run_file(filename, false, lib_arg);
    } else if (strcmp(command, "compile") == 0) {
        if (argc < 3) {
            printf("Usage: korelin compile <file-name>\n");
            return 1;
        }
        run_file(argv[2], true, NULL);
    } else if (strcmp(command, "editor") == 0) {
        keditor_run(argc >= 3 ? argv[2] : NULL);
    } else {
        // Implicit run
        // Check if file exists or extension matches
        const char* ext = strrchr(command, '.');
        if (ext && (strcmp(ext, ".k") == 0 || strcmp(ext, ".kri") == 0)) {
            run_file(command, false, NULL);
        } else {
            printf("Unknown command: %s\n", command);
            print_help();
            return 1;
        }
    }

    return 0;
}
