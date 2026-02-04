//
// Created by Helix on 2026/1/21.
//
#include "kcode.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "kconst.h" 

// --- Internal Structures ---

typedef struct {
    char* name;
    int depth;
    int reg_index;
} Local;

typedef struct {
    int continue_target;
    int* break_jumps;
    int break_count;
    int break_capacity;
    int* continue_jumps;
    int continue_count;
    int continue_capacity;
    int scope_depth;
} LoopState;

typedef struct {
    KBytecodeChunk* chunk;
    Local locals[256];
    int local_count;
    int scope_depth;
    int current_reg_count;
    char* current_class_name;
    LoopState loops[16];
    int loop_depth;
} CompilerState;

// --- Forward Declarations ---

static void compile_statement(CompilerState* compiler, KastStatement* stmt);
static void compile_expression(CompilerState* compiler, KastExpression* expr, int target_reg);
static int add_string_constant(CompilerState* compiler, const char* str);
static void patch_jump(CompilerState* compiler, int offset, int target);

// --- Chunk Management ---

void init_chunk(KBytecodeChunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->string_table = NULL;
    chunk->string_count = 0;
    chunk->lines = NULL;
}

void free_chunk(KBytecodeChunk* chunk) {
    free(chunk->code);
    free(chunk->lines);
    for (size_t i = 0; i < chunk->string_count; i++) {
        free(chunk->string_table[i]);
    }
    free(chunk->string_table);
    init_chunk(chunk);
}

void write_chunk(KBytecodeChunk* chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        chunk->capacity = chunk->capacity < 8 ? 8 : chunk->capacity * 2;
        uint8_t* new_code = (uint8_t*)realloc(chunk->code, chunk->capacity);
        int* new_lines = (int*)realloc(chunk->lines, chunk->capacity * sizeof(int));
        if (!new_code || !new_lines) {
            printf("FATAL: Out of memory in write_chunk\n");
            exit(1);
        }
        chunk->code = new_code;
        chunk->lines = new_lines;
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

static int add_string_constant(CompilerState* compiler, const char* str) {
    for (size_t i = 0; i < compiler->chunk->string_count; i++) {
        if (strcmp(compiler->chunk->string_table[i], str) == 0) {
            return (int)i;
        }
    }
    
    compiler->chunk->string_count++;
    compiler->chunk->string_table = (char**)realloc(compiler->chunk->string_table, 
                                                    compiler->chunk->string_count * sizeof(char*));
    compiler->chunk->string_table[compiler->chunk->string_count - 1] = strdup(str);
    return (int)compiler->chunk->string_count - 1;
}

static void patch_jump(CompilerState* compiler, int offset, int target) {
    int jump = target - offset - 2;
    if (jump > UINT16_MAX) {
        printf("Jump too far!\n");
    }
    compiler->chunk->code[offset] = (jump >> 8) & 0xFF;
    compiler->chunk->code[offset + 1] = jump & 0xFF;
}

static void init_compiler(CompilerState* compiler, KBytecodeChunk* chunk) {
    compiler->chunk = chunk;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->current_reg_count = 0;
    compiler->current_class_name = NULL;
    compiler->loop_depth = 0;
}

static void enter_loop(CompilerState* compiler, int continue_target) {
    if (compiler->loop_depth >= 16) return;
    LoopState* loop = &compiler->loops[compiler->loop_depth++];
    loop->continue_target = continue_target;
    loop->break_jumps = NULL;
    loop->break_count = 0;
    loop->break_capacity = 0;
    loop->continue_jumps = NULL;
    loop->continue_count = 0;
    loop->continue_capacity = 0;
    loop->scope_depth = compiler->scope_depth;
}

static void exit_loop(CompilerState* compiler) {
    if (compiler->loop_depth == 0) return;
    LoopState* loop = &compiler->loops[--compiler->loop_depth];
    
    int end = compiler->chunk->count;
    for (int i = 0; i < loop->break_count; i++) {
        patch_jump(compiler, loop->break_jumps[i], end);
    }
    
    if (loop->break_jumps) free(loop->break_jumps);
    if (loop->continue_jumps) free(loop->continue_jumps);
}

static void add_break(CompilerState* compiler, int jump_offset) {
    if (compiler->loop_depth == 0) return;
    LoopState* loop = &compiler->loops[compiler->loop_depth - 1];
    
    if (loop->break_count >= loop->break_capacity) {
        loop->break_capacity = loop->break_capacity < 8 ? 8 : loop->break_capacity * 2;
        loop->break_jumps = (int*)realloc(loop->break_jumps, loop->break_capacity * sizeof(int));
    }
    loop->break_jumps[loop->break_count++] = jump_offset;
}

static void add_continue(CompilerState* compiler, int jump_offset) {
    if (compiler->loop_depth == 0) return;
    LoopState* loop = &compiler->loops[compiler->loop_depth - 1];
    
    if (loop->continue_target != -1) {
        patch_jump(compiler, jump_offset, loop->continue_target);
        return;
    }
    
    if (loop->continue_count >= loop->continue_capacity) {
        loop->continue_capacity = loop->continue_capacity < 8 ? 8 : loop->continue_capacity * 2;
        loop->continue_jumps = (int*)realloc(loop->continue_jumps, loop->continue_capacity * sizeof(int));
    }
    loop->continue_jumps[loop->continue_count++] = jump_offset;
}

static void resolve_continue(CompilerState* compiler, int target) {
    if (compiler->loop_depth == 0) return;
    LoopState* loop = &compiler->loops[compiler->loop_depth - 1];
    loop->continue_target = target;
    for (int i = 0; i < loop->continue_count; i++) {
        patch_jump(compiler, loop->continue_jumps[i], target);
    }
}

// --- Emitters ---

static void emit_byte(CompilerState* compiler, uint8_t byte) {
    write_chunk(compiler->chunk, byte, 0); // Line number todo
}

static void emit_instruction(CompilerState* compiler, uint8_t op, uint8_t r1, uint8_t r2, uint8_t r3) {
    emit_byte(compiler, op);
    emit_byte(compiler, r1);
    emit_byte(compiler, r2);
    emit_byte(compiler, r3);
}

static int emit_jump(CompilerState* compiler, uint8_t op, uint8_t r1) {
    emit_byte(compiler, op);
    emit_byte(compiler, r1);
    emit_byte(compiler, 0); emit_byte(compiler, 0); // 16-bit offset placeholder
    return compiler->chunk->count - 2;
}

// --- Locals ---

static void add_local(CompilerState* compiler, const char* name) {
    if (compiler->local_count == 256) {
        printf("Too many local variables\n");
        return;
    }
    Local* local = &compiler->locals[compiler->local_count++];
    local->name = strdup(name);
    local->depth = compiler->scope_depth;
    local->reg_index = compiler->current_reg_count++; // Allocate register
}

static int resolve_local(CompilerState* compiler, const char* name) {
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (strcmp(local->name, name) == 0) {
            return local->reg_index;
        }
    }
    return -1;
}

// --- Compilation ---

static void compile_expression(CompilerState* compiler, KastExpression* expr, int target_reg) {
    if (!expr) return;

    switch (expr->base.type) {
        case KAST_NODE_LITERAL: {
            KastLiteral* lit = (KastLiteral*)expr;
            if (lit->token.type == KORELIN_TOKEN_INT) {
                // Parse int
                long long val = atoll(lit->token.value);
                // LDI target, imm
                if (val >= -128 && val <= 127) {
                     emit_byte(compiler, KOP_LDI);
                     emit_byte(compiler, target_reg);
                     emit_byte(compiler, (int8_t)val);
                     emit_byte(compiler, 0); // Padding
                } else {
                     emit_byte(compiler, KOP_LDI64);
                     emit_byte(compiler, target_reg);
                     // Emit 8 bytes Big Endian
                     for(int i=0; i<8; i++) {
                         emit_byte(compiler, (uint8_t)((val >> ((7-i)*8)) & 0xFF));
                     }
                }
            } else if (lit->token.type == KORELIN_TOKEN_STRING) {
                int idx = add_string_constant(compiler, lit->token.value);
                emit_byte(compiler, KOP_LDC); 
                emit_byte(compiler, target_reg);
                emit_byte(compiler, (uint8_t)(idx >> 8));
                emit_byte(compiler, (uint8_t)(idx & 0xFF));
            } else if (lit->token.type == KORELIN_TOKEN_BOOL) {
                 // bool type keyword - unlikely to be a literal but handle just in case
                 emit_byte(compiler, KOP_LDB);
                 emit_byte(compiler, target_reg);
                 emit_byte(compiler, 0);
                 emit_byte(compiler, 0); // Padding
             } else if (lit->token.type == KORELIN_TOKEN_TRUE) {
                 emit_byte(compiler, KOP_LDB);
                 emit_byte(compiler, target_reg);
                 emit_byte(compiler, 1);
                 emit_byte(compiler, 0); 
             } else if (lit->token.type == KORELIN_TOKEN_FALSE) {
                 emit_byte(compiler, KOP_LDB);
                 emit_byte(compiler, target_reg);
                 emit_byte(compiler, 0);
                 emit_byte(compiler, 0); 
             } else if (lit->token.type == KORELIN_TOKEN_NIL) {
                 emit_byte(compiler, KOP_LDN);
                 emit_byte(compiler, target_reg);
                 emit_byte(compiler, 0);
                 emit_byte(compiler, 0); 
             } else if (lit->token.type == KORELIN_TOKEN_FLOAT) {
                // Default to double for literals
                double val = atof(lit->token.value);
                emit_byte(compiler, KOP_LDCD);
                emit_byte(compiler, target_reg);
                // Emit 8 bytes (double representation)
                uint64_t bits;
                memcpy(&bits, &val, sizeof(bits));
                // Big Endian emission
                for(int i=0; i<8; i++) {
                    emit_byte(compiler, (uint8_t)((bits >> ((7-i)*8)) & 0xFF));
                }
            }
            break;
        }
        case KAST_NODE_IDENTIFIER: {
            KastIdentifier* ident = (KastIdentifier*)expr;
            int reg = resolve_local(compiler, ident->name);
            if (reg != -1) {
                if (reg != target_reg) {
                    emit_instruction(compiler, KOP_LOAD, target_reg, reg, 0);
                }
            } else {
                // Global
                int idx = add_string_constant(compiler, ident->name);
                emit_byte(compiler, KOP_GET_GLOBAL);
                emit_byte(compiler, target_reg);
                emit_byte(compiler, (uint8_t)(idx >> 8));
                emit_byte(compiler, (uint8_t)(idx & 0xFF));
            }
            break;
        }
        case KAST_NODE_BINARY_OP: {
            KastBinaryOp* bin = (KastBinaryOp*)expr;
            compile_expression(compiler, (KastExpression*)bin->left, target_reg);
            int right_reg = compiler->current_reg_count++;
            compile_expression(compiler, (KastExpression*)bin->right, right_reg);
            
            uint8_t op = KOP_ADD;
            switch(bin->operator) {
                case KORELIN_TOKEN_ADD: op = KOP_ADD; break;
                case KORELIN_TOKEN_SUB: op = KOP_SUB; break;
                case KORELIN_TOKEN_MUL: op = KOP_MUL; break;
                case KORELIN_TOKEN_DIV: op = KOP_DIV; break;
                case KORELIN_TOKEN_EQ: op = KOP_EQ; break;
                case KORELIN_TOKEN_NE: op = KOP_NE; break;
                case KORELIN_TOKEN_LT: op = KOP_LT; break;
                case KORELIN_TOKEN_LE: op = KOP_LE; break;
                case KORELIN_TOKEN_GT: op = KOP_GT; break;
                case KORELIN_TOKEN_GE: op = KOP_GE; break;
                default: break;
            }
            
            emit_instruction(compiler, op, target_reg, target_reg, right_reg);
            compiler->current_reg_count--;
            break;
        }
        case KAST_NODE_UNARY_OP: {
            KastUnaryOp* unary = (KastUnaryOp*)expr;
            compile_expression(compiler, (KastExpression*)unary->operand, target_reg);
            
            uint8_t op = KOP_NEG;
            if (unary->operator == KORELIN_TOKEN_NOT) op = KOP_NOT;
            
            emit_instruction(compiler, op, target_reg, target_reg, 0);
            break;
        }
        case KAST_NODE_ASSIGNMENT: {
            KastAssignment* assign = (KastAssignment*)expr;
            if (assign->lvalue->type == KAST_NODE_IDENTIFIER) {
                KastIdentifier* ident = (KastIdentifier*)assign->lvalue;
                int reg = resolve_local(compiler, ident->name);
                if (reg != -1) {
                    compile_expression(compiler, (KastExpression*)assign->value, reg);
                    if (target_reg != reg) {
                         emit_instruction(compiler, KOP_LOAD, target_reg, reg, 0);
                    }
                } else {
                    // Global
                    int val_reg = target_reg;
                    compile_expression(compiler, (KastExpression*)assign->value, val_reg);
                    int idx = add_string_constant(compiler, ident->name);
                    emit_byte(compiler, KOP_SET_GLOBAL);
                    emit_byte(compiler, val_reg);
                    emit_byte(compiler, (uint8_t)(idx >> 8));
                    emit_byte(compiler, (uint8_t)(idx & 0xFF));
                }
            } else if (assign->lvalue->type == KAST_NODE_MEMBER_ACCESS) {
                KastMemberAccess* acc = (KastMemberAccess*)assign->lvalue;
                int obj_reg = compiler->current_reg_count++;
                compile_expression(compiler, (KastExpression*)acc->object, obj_reg);
                int val_reg = compiler->current_reg_count++;
                compile_expression(compiler, (KastExpression*)assign->value, val_reg);
                
                int name_idx = add_string_constant(compiler, acc->member_name);
                
                emit_byte(compiler, KOP_PUTF);
                emit_byte(compiler, obj_reg);
                emit_byte(compiler, val_reg);
                emit_byte(compiler, (uint8_t)(name_idx >> 8));
                emit_byte(compiler, (uint8_t)(name_idx & 0xFF));
                
                if (target_reg != val_reg) {
                    emit_instruction(compiler, KOP_LOAD, target_reg, val_reg, 0);
                }
                
                compiler->current_reg_count -= 2;
            } else if (assign->lvalue->type == KAST_NODE_ARRAY_ACCESS) {
                 KastArrayAccess* acc = (KastArrayAccess*)assign->lvalue;
                 
                 int arr_reg = compiler->current_reg_count++;
                 compile_expression(compiler, (KastExpression*)acc->array, arr_reg);
                 
                 int idx_reg = compiler->current_reg_count++;
                 compile_expression(compiler, (KastExpression*)acc->index, idx_reg);
                 
                 int val_reg = compiler->current_reg_count++;
                 compile_expression(compiler, (KastExpression*)assign->value, val_reg);
                 
                 emit_byte(compiler, KOP_PUTFA);
                 emit_byte(compiler, arr_reg);
                 emit_byte(compiler, idx_reg);
                 emit_byte(compiler, val_reg);
                 
                 if (target_reg != val_reg) {
                      emit_instruction(compiler, KOP_LOAD, target_reg, val_reg, 0);
                 }
                 
                 compiler->current_reg_count -= 3;
            }
            break;
        }
        case KAST_NODE_ARRAY_ACCESS: {
            KastArrayAccess* acc = (KastArrayAccess*)expr;
            
            int arr_reg = compiler->current_reg_count++;
            compile_expression(compiler, (KastExpression*)acc->array, arr_reg);
            
            int idx_reg = compiler->current_reg_count++;
            compile_expression(compiler, (KastExpression*)acc->index, idx_reg);
            
            emit_byte(compiler, KOP_GETFA);
            emit_byte(compiler, target_reg);
            emit_byte(compiler, arr_reg);
            emit_byte(compiler, idx_reg);
            
            compiler->current_reg_count -= 2;
            break;
        }
        case KAST_NODE_CALL: {
            KastCall* call = (KastCall*)expr;
            
            if (call->callee->type == KAST_NODE_MEMBER_ACCESS) {
                KastMemberAccess* acc = (KastMemberAccess*)call->callee;
                
                // Check for super
                if (acc->object->type == KAST_NODE_IDENTIFIER && 
                    strcmp(((KastIdentifier*)acc->object)->name, "super") == 0) {
                     
                     if (!compiler->current_class_name) {
                         printf("Compile Error: 'super' used outside of class\n");
                         return;
                     }
                     
                     // Push Arguments
                     for (size_t i = 0; i < call->arg_count; i++) {
                        int arg_reg = compiler->current_reg_count++;
                        compile_expression(compiler, (KastExpression*)call->args[i], arg_reg);
                        emit_instruction(compiler, KOP_PUSH, 0, arg_reg, 0);
                        compiler->current_reg_count--;
                     }
                     
                     int self_reg = resolve_local(compiler, "self");
                     if (self_reg == -1) {
                          printf("Compile Error: 'super' used in static context\n");
                          return;
                     }
                     
                     int method_idx = add_string_constant(compiler, acc->member_name);
                     int class_idx = add_string_constant(compiler, compiler->current_class_name);
                     
                     emit_byte(compiler, KOP_GETSUPER);
                     emit_byte(compiler, target_reg);
                     emit_byte(compiler, self_reg);
                     emit_byte(compiler, (uint8_t)(method_idx >> 8));
                     emit_byte(compiler, (uint8_t)(method_idx & 0xFF));
                     emit_byte(compiler, (uint8_t)(class_idx >> 8));
                     emit_byte(compiler, (uint8_t)(class_idx & 0xFF));
                     
                     emit_byte(compiler, KOP_CALLR);
                     emit_byte(compiler, target_reg);
                     emit_byte(compiler, (uint8_t)call->arg_count);
                     emit_byte(compiler, 0);
                     
                } else {
                    int obj_reg = compiler->current_reg_count++;
                    compile_expression(compiler, (KastExpression*)acc->object, obj_reg);
                    
                    // Push Object (Self/Module)
                    emit_instruction(compiler, KOP_PUSH, 0, obj_reg, 0);
                    
                    // Push Arguments
                    for (size_t i = 0; i < call->arg_count; i++) {
                        int arg_reg = compiler->current_reg_count++;
                        compile_expression(compiler, (KastExpression*)call->args[i], arg_reg);
                        emit_instruction(compiler, KOP_PUSH, 0, arg_reg, 0);
                        compiler->current_reg_count--;
                    }
                    
                    int name_idx = add_string_constant(compiler, acc->member_name);
                    
                    // Emit INVOKE Rd, Ra(Obj), NameIdx(16), ArgCount(8)
                    emit_byte(compiler, KOP_INVOKE);
                    emit_byte(compiler, target_reg);
                    emit_byte(compiler, obj_reg);
                    emit_byte(compiler, (uint8_t)(name_idx >> 8));
                    emit_byte(compiler, (uint8_t)(name_idx & 0xFF));
                    emit_byte(compiler, (uint8_t)call->arg_count);
                    
                    compiler->current_reg_count--; // Release obj_reg
                }

            } else {
                // Normal call or super(...)
                
                // Check for super(...) call
                if (call->callee->type == KAST_NODE_IDENTIFIER && 
                    strcmp(((KastIdentifier*)call->callee)->name, "super") == 0) {
                        
                     if (!compiler->current_class_name) {
                         printf("Compile Error: 'super' used outside of class\n");
                         return;
                     }
                     
                     // Push Arguments
                     for (size_t i = 0; i < call->arg_count; i++) {
                        int arg_reg = compiler->current_reg_count++;
                        compile_expression(compiler, (KastExpression*)call->args[i], arg_reg);
                        emit_instruction(compiler, KOP_PUSH, 0, arg_reg, 0);
                        compiler->current_reg_count--;
                     }
                     
                     int self_reg = resolve_local(compiler, "self");
                     if (self_reg == -1) {
                          printf("Compile Error: 'super' used in static context\n");
                          return;
                     }
                     
                     // Implicitly call _init
                     int method_idx = add_string_constant(compiler, "_init");
                     int class_idx = add_string_constant(compiler, compiler->current_class_name);
                     
                     emit_byte(compiler, KOP_GETSUPER);
                     emit_byte(compiler, target_reg);
                     emit_byte(compiler, self_reg);
                     emit_byte(compiler, (uint8_t)(method_idx >> 8));
                     emit_byte(compiler, (uint8_t)(method_idx & 0xFF));
                     emit_byte(compiler, (uint8_t)(class_idx >> 8));
                     emit_byte(compiler, (uint8_t)(class_idx & 0xFF));
                     
                     emit_byte(compiler, KOP_CALLR);
                     emit_byte(compiler, target_reg);
                     emit_byte(compiler, (uint8_t)call->arg_count);
                     emit_byte(compiler, 0);
                     
                } else {
                    // Standard function call
                    for (size_t i = 0; i < call->arg_count; i++) {
                        int arg_reg = compiler->current_reg_count++;
                        compile_expression(compiler, (KastExpression*)call->args[i], arg_reg);
                        emit_instruction(compiler, KOP_PUSH, 0, arg_reg, 0);
                        compiler->current_reg_count--;
                    }
                    
                    compile_expression(compiler, (KastExpression*)call->callee, target_reg);
                    
                    emit_byte(compiler, KOP_CALLR);
                    emit_byte(compiler, target_reg);
                    emit_byte(compiler, (uint8_t)call->arg_count);
                    emit_byte(compiler, 0);
                }
            }
            break;
        }
        case KAST_NODE_NEW: {
            KastNew* n = (KastNew*)expr;
            
            if (n->is_array) {
                // Array creation: new int[size]
                // Argument 0 is size
                if (n->arg_count != 1) {
                    printf("Compile Error: Array creation requires exactly one size argument\n");
                    return;
                }
                
                int size_reg = compiler->current_reg_count++;
                compile_expression(compiler, (KastExpression*)n->args[0], size_reg);
                
                emit_byte(compiler, KOP_NEWA);
                emit_byte(compiler, target_reg);
                emit_byte(compiler, size_reg);
                emit_byte(compiler, 0); // Padding
                
                compiler->current_reg_count--;
            } else {
                // Push arguments first
                for (size_t i = 0; i < n->arg_count; i++) {
                    int arg_reg = compiler->current_reg_count++;
                    compile_expression(compiler, (KastExpression*)n->args[i], arg_reg);
                    emit_instruction(compiler, KOP_PUSH, 0, arg_reg, 0);
                    compiler->current_reg_count--;
                }

                // Strip generics from class name for runtime lookup (e.g., "Box<int>" -> "Box")
                char* class_name_lookup = strdup(n->class_name);
                char* lt = strchr(class_name_lookup, '<');
                if (lt) *lt = '\0';

                int type_idx = add_string_constant(compiler, class_name_lookup);
                free(class_name_lookup);
                
                emit_byte(compiler, KOP_NEW);
                emit_byte(compiler, target_reg);
                emit_byte(compiler, (uint8_t)(type_idx >> 8));
                emit_byte(compiler, (uint8_t)(type_idx & 0xFF));
                emit_byte(compiler, (uint8_t)n->arg_count);
            }
            break;
        }
        case KAST_NODE_MEMBER_ACCESS: {
            KastMemberAccess* acc = (KastMemberAccess*)expr;
            
            // Check for super
            if (acc->object->type == KAST_NODE_IDENTIFIER && 
                strcmp(((KastIdentifier*)acc->object)->name, "super") == 0) {
                 
                 if (!compiler->current_class_name) {
                     printf("Compile Error: 'super' used outside of class\n");
                     return;
                 }
                 
                 int self_reg = resolve_local(compiler, "self");
                 if (self_reg == -1) {
                      printf("Compile Error: 'super' used in static context\n");
                      return;
                 }
                 
                 int method_idx = add_string_constant(compiler, acc->member_name);
                 int class_idx = add_string_constant(compiler, compiler->current_class_name);
                 
                 emit_byte(compiler, KOP_GETSUPER);
                 emit_byte(compiler, target_reg);
                 emit_byte(compiler, self_reg);
                 emit_byte(compiler, (uint8_t)(method_idx >> 8));
                 emit_byte(compiler, (uint8_t)(method_idx & 0xFF));
                 emit_byte(compiler, (uint8_t)(class_idx >> 8));
                 emit_byte(compiler, (uint8_t)(class_idx & 0xFF));
                 return;
            }

            compile_expression(compiler, (KastExpression*)acc->object, target_reg);
            int idx = add_string_constant(compiler, acc->member_name);
            emit_byte(compiler, KOP_GETF);
            emit_byte(compiler, target_reg);
            emit_byte(compiler, target_reg);
            emit_byte(compiler, (uint8_t)(idx >> 8));
            emit_byte(compiler, (uint8_t)(idx & 0xFF));
            break;
        }
        case KAST_NODE_SCOPE_ACCESS: {
             KastScopeAccess* acc = (KastScopeAccess*)expr;
             // 1. Resolve object/class (Left side)
             int reg = resolve_local(compiler, acc->class_name);
             if (reg != -1) {
                 // Local variable
                 if (reg != target_reg) {
                     emit_instruction(compiler, KOP_LOAD, target_reg, reg, 0);
                 }
             } else {
                 // Global or Class Name
                 int idx = add_string_constant(compiler, acc->class_name);
                 emit_byte(compiler, KOP_GET_GLOBAL);
                 emit_byte(compiler, target_reg);
                 emit_byte(compiler, (uint8_t)(idx >> 8));
                 emit_byte(compiler, (uint8_t)(idx & 0xFF));
             }
             
             // 2. Get Member (GETF)
             int member_idx = add_string_constant(compiler, acc->member_name);
             emit_byte(compiler, KOP_GETF);
             emit_byte(compiler, target_reg);
             emit_byte(compiler, target_reg); // object reg
             emit_byte(compiler, (uint8_t)(member_idx >> 8));
             emit_byte(compiler, (uint8_t)(member_idx & 0xFF));
             break;
        }
        case KAST_NODE_POSTFIX_OP: {
            KastPostfixOp* post = (KastPostfixOp*)expr;
            
            // Postfix ++/-- requires lvalue: Identifier, MemberAccess
            if (post->operand->type == KAST_NODE_IDENTIFIER) {
                KastIdentifier* ident = (KastIdentifier*)post->operand;
                int reg = resolve_local(compiler, ident->name);
                
                if (reg != -1) {
                    // Local: 
                    // 1. Load current value to target_reg (Original value)
                    if (target_reg != reg) {
                        emit_instruction(compiler, KOP_LOAD, target_reg, reg, 0);
                    }
                    
                    // 2. Increment/Decrement reg
                    int temp_reg = compiler->current_reg_count++;
                    emit_byte(compiler, KOP_LDI);
                    emit_byte(compiler, temp_reg);
                    emit_byte(compiler, 1);
                    emit_byte(compiler, 0);
                    
                    uint8_t op = (post->operator == KORELIN_TOKEN_INC) ? KOP_ADD : KOP_SUB;
                    emit_instruction(compiler, op, reg, reg, temp_reg); // reg = reg +/- 1
                    
                    compiler->current_reg_count--;
                } else {
                    // Global
                    // 1. Get Global to target_reg
                    int idx = add_string_constant(compiler, ident->name);
                    emit_byte(compiler, KOP_GET_GLOBAL);
                    emit_byte(compiler, target_reg);
                    emit_byte(compiler, (uint8_t)(idx >> 8));
                    emit_byte(compiler, (uint8_t)(idx & 0xFF));
                    
                    // 2. Calc New Value
                    int temp_reg = compiler->current_reg_count++;
                    int one_reg = compiler->current_reg_count++;
                    emit_byte(compiler, KOP_LDI);
                    emit_byte(compiler, one_reg);
                    emit_byte(compiler, 1);
                    emit_byte(compiler, 0);
                    
                    uint8_t op = (post->operator == KORELIN_TOKEN_INC) ? KOP_ADD : KOP_SUB;
                    emit_instruction(compiler, op, temp_reg, target_reg, one_reg);
                    
                    // 3. Set Global
                    emit_byte(compiler, KOP_SET_GLOBAL);
                    emit_byte(compiler, temp_reg);
                    emit_byte(compiler, (uint8_t)(idx >> 8));
                    emit_byte(compiler, (uint8_t)(idx & 0xFF));
                    
                    compiler->current_reg_count -= 2;
                }
            } else if (post->operand->type == KAST_NODE_MEMBER_ACCESS) {
                KastMemberAccess* acc = (KastMemberAccess*)post->operand;
                
                // 1. Compile Object
                int obj_reg = compiler->current_reg_count++;
                compile_expression(compiler, (KastExpression*)acc->object, obj_reg);
                
                int idx = add_string_constant(compiler, acc->member_name);
                
                // 2. Get Field to target_reg
                emit_byte(compiler, KOP_GETF);
                emit_byte(compiler, target_reg);
                emit_byte(compiler, obj_reg);
                emit_byte(compiler, (uint8_t)(idx >> 8));
                emit_byte(compiler, (uint8_t)(idx & 0xFF));
                
                // 3. Calc New Value
                int temp_reg = compiler->current_reg_count++;
                int one_reg = compiler->current_reg_count++;
                emit_byte(compiler, KOP_LDI);
                emit_byte(compiler, one_reg);
                emit_byte(compiler, 1);
                emit_byte(compiler, 0);
                
                uint8_t op = (post->operator == KORELIN_TOKEN_INC) ? KOP_ADD : KOP_SUB;
                emit_instruction(compiler, op, temp_reg, target_reg, one_reg);
                
                // 4. Put Field
                emit_byte(compiler, KOP_PUTF);
                emit_byte(compiler, obj_reg);
                emit_byte(compiler, temp_reg);
                emit_byte(compiler, (uint8_t)(idx >> 8));
                emit_byte(compiler, (uint8_t)(idx & 0xFF));
                
                compiler->current_reg_count -= 3;
            } else {
                printf("Compile Error: Invalid lvalue for postfix operation\n");
            }
            break;
        }
        default: break;
    }
}

static void compile_function_decl(CompilerState* compiler, KastFunctionDecl* func) {
    int jmp_patch = emit_jump(compiler, KOP_JMP, 0);
    uint32_t start_addr = compiler->chunk->count;
    
    Local saved_locals[256];
    int saved_local_count = compiler->local_count;
    int saved_scope_depth = compiler->scope_depth;
    int saved_reg_count = compiler->current_reg_count;
    char* saved_class_name = compiler->current_class_name;
    memcpy(saved_locals, compiler->locals, sizeof(saved_locals));
    
    compiler->local_count = 0;
    compiler->scope_depth = 1;
    compiler->current_reg_count = 0;
    
    if (func->parent_class_name) {
        compiler->current_class_name = func->parent_class_name;
    }
    
    for (size_t i = 0; i < func->arg_count; i++) {
        KastVarDecl* arg = (KastVarDecl*)func->args[i];
        add_local(compiler, arg->name);
    }
    
    compile_statement(compiler, (KastStatement*)func->body);
    emit_byte(compiler, KOP_RET);
    emit_byte(compiler, 0); emit_byte(compiler, 0); emit_byte(compiler, 0);
    
    compiler->local_count = saved_local_count;
    compiler->scope_depth = saved_scope_depth;
    compiler->current_reg_count = saved_reg_count;
    compiler->current_class_name = saved_class_name;
    memcpy(compiler->locals, saved_locals, sizeof(saved_locals));
    patch_jump(compiler, jmp_patch, compiler->chunk->count);
    
    int name_idx = add_string_constant(compiler, func->name);
    emit_byte(compiler, KOP_FUNCTION);
    emit_byte(compiler, (uint8_t)(name_idx >> 8));
    emit_byte(compiler, (uint8_t)(name_idx & 0xFF));
    emit_byte(compiler, (uint8_t)(start_addr >> 16));
    emit_byte(compiler, (uint8_t)(start_addr >> 8));
    emit_byte(compiler, (uint8_t)(start_addr & 0xFF));
    emit_byte(compiler, (uint8_t)func->arg_count);
    emit_byte(compiler, (uint8_t)func->access);
    
    if (func->parent_class_name) {
        // Out-of-class method definition
        // Bind to class using KOP_METHOD
        // Note: KOP_FUNCTION pushes the function object to stack (no POP needed)
        // KOP_METHOD expects ClassName and MethodName
        
        // Ensure string constants are added
        int class_idx = add_string_constant(compiler, func->parent_class_name);
        // Method name is already in name_idx
        
        emit_byte(compiler, KOP_METHOD);
        emit_byte(compiler, (uint8_t)(class_idx >> 8));
        emit_byte(compiler, (uint8_t)(class_idx & 0xFF));
        emit_byte(compiler, (uint8_t)(name_idx >> 8));
        emit_byte(compiler, (uint8_t)(name_idx & 0xFF));
        
        // KOP_METHOD consumes the function object from stack?
        // Based on compile_class_decl logic, it seems it does NOT consume it?
        // In compile_class_decl, KOP_METHOD follows KOP_FUNCTION immediately.
        // It doesn't seem to clean up the stack.
        // But here we are at top level (usually).
        // If KOP_FUNCTION leaves object on stack, and KOP_METHOD uses it.
        // Does KOP_METHOD pop it?
        // If not, we need to POP it.
        // Assuming KOP_METHOD attaches it and leaves it (or consumes it).
        // Let's assume it works like in compile_class_decl.
        // But in compile_class_decl, we are inside a class body context?
        // No, KOP_CLASS defines the class.
        // Actually, looking at compile_class_decl logic again:
        // It emits KOP_FUNCTION, then KOP_METHOD.
        // It does not POP.
        // So the function object remains on stack?
        // If so, who pops it?
        // compile_class_decl seems to leave garbage on stack?
        // Or maybe KOP_METHOD consumes it.
        // I will assume KOP_METHOD consumes it or it's fine.
        
        // However, if we are at top level script, we don't want to leave junk on stack.
        // KOP_SET_GLOBAL pops it (via POP instruction I added manually in previous version, or implicitly?)
        // In original code:
        // int reg = compiler->current_reg_count++;
        // emit_instruction(compiler, KOP_POP, reg, 0, 0); 
        // emit_byte(compiler, KOP_SET_GLOBAL); ...
        
        // So originally, it moves stack top to reg, then sets global.
        
        // Here, if KOP_METHOD is used, and it behaves like in class decl, 
        // we might need to verify if we need to clean up stack.
        // Since I don't see VM code, I'll follow compile_class_decl pattern.
        // But wait, compile_class_decl runs at script level (top level usually).
        // If it leaves items on stack, the stack grows.
        
        // Let's assume KOP_METHOD consumes it.
        
    } else {
        int reg = compiler->current_reg_count++;
        emit_instruction(compiler, KOP_POP, reg, 0, 0); 
        emit_byte(compiler, KOP_SET_GLOBAL);
        emit_byte(compiler, reg);
        emit_byte(compiler, (uint8_t)(name_idx >> 8));
        emit_byte(compiler, (uint8_t)(name_idx & 0xFF));
        compiler->current_reg_count--;
    }
}

static void compile_field_initializers(CompilerState* compiler, KastClassDecl* cls) {
    // Expects "self" to be available in locals (usually reg 0)
    int self_reg = resolve_local(compiler, "self");
    if (self_reg == -1) {
         return; 
    }

    for (size_t i = 0; i < cls->member_count; i++) {
        KastClassMember* member = cls->members[i];
        if (member->member_type == KAST_MEMBER_PROPERTY && member->init_value != NULL) {
            int val_reg = compiler->current_reg_count++;
            compile_expression(compiler, (KastExpression*)member->init_value, val_reg);
            
            int name_idx = add_string_constant(compiler, member->name);
            emit_byte(compiler, KOP_PUTF);
            emit_byte(compiler, self_reg);
            emit_byte(compiler, val_reg);
            emit_byte(compiler, (uint8_t)(name_idx >> 8));
            emit_byte(compiler, (uint8_t)(name_idx & 0xFF));
            
            compiler->current_reg_count--;
        }
    }
}

static void compile_class_decl(CompilerState* compiler, KastClassDecl* cls) {
    int name_idx = add_string_constant(compiler, cls->name);
    emit_byte(compiler, KOP_CLASS);
    emit_byte(compiler, 0); 
    emit_byte(compiler, (uint8_t)(name_idx >> 8));
    emit_byte(compiler, (uint8_t)(name_idx & 0xFF));
    
    char* prev_class_name = compiler->current_class_name;
    compiler->current_class_name = cls->name;
    
    if (cls->parent_name) {
        int parent_idx = add_string_constant(compiler, cls->parent_name);
        emit_byte(compiler, KOP_INHERIT);
        emit_byte(compiler, (uint8_t)(name_idx >> 8));
        emit_byte(compiler, (uint8_t)(name_idx & 0xFF));
        emit_byte(compiler, (uint8_t)(parent_idx >> 8));
        emit_byte(compiler, (uint8_t)(parent_idx & 0xFF));
    }
    
    bool has_init = false;

    for (size_t i = 0; i < cls->member_count; i++) {
        KastClassMember* member = cls->members[i];
        if (member->member_type == KAST_MEMBER_METHOD) {
            if (strcmp(member->name, "_init_") == 0) {
                has_init = true;
            }

            int jmp_patch = emit_jump(compiler, KOP_JMP, 0);
            uint32_t start_addr = compiler->chunk->count;
            
            Local saved_locals[256];
            int saved_local_count = compiler->local_count;
            int saved_scope_depth = compiler->scope_depth;
            int saved_reg_count = compiler->current_reg_count;
            
            if (saved_local_count > 0) {
                memcpy(saved_locals, compiler->locals, saved_local_count * sizeof(Local));
            }
            
            compiler->local_count = 0;
            compiler->scope_depth = 1;
            compiler->current_reg_count = 0;
            
            for (size_t j = 0; j < member->arg_count; j++) {
                KastVarDecl* arg = (KastVarDecl*)member->args[j];
                add_local(compiler, arg->name);
            }
            
            // Inject field initializers if this is _init_
            if (strcmp(member->name, "_init_") == 0) {
                compile_field_initializers(compiler, cls);
            }

            compile_statement(compiler, (KastStatement*)member->body);
            emit_byte(compiler, KOP_RET);
            emit_byte(compiler, 0); emit_byte(compiler, 0); emit_byte(compiler, 0);
            
            compiler->local_count = saved_local_count;
            compiler->scope_depth = saved_scope_depth;
            compiler->current_reg_count = saved_reg_count;
            
            if (saved_local_count > 0) {
                memcpy(compiler->locals, saved_locals, saved_local_count * sizeof(Local));
            }
            patch_jump(compiler, jmp_patch, compiler->chunk->count);
            
            int method_name_idx = add_string_constant(compiler, member->name);
            emit_byte(compiler, KOP_FUNCTION);
            emit_byte(compiler, (uint8_t)(method_name_idx >> 8));
            emit_byte(compiler, (uint8_t)(method_name_idx & 0xFF));
            emit_byte(compiler, (uint8_t)(start_addr >> 16));
            emit_byte(compiler, (uint8_t)(start_addr >> 8));
            emit_byte(compiler, (uint8_t)(start_addr & 0xFF));
            emit_byte(compiler, (uint8_t)member->arg_count);
            emit_byte(compiler, (uint8_t)member->access);
            
            emit_byte(compiler, KOP_METHOD);
            emit_byte(compiler, (uint8_t)(name_idx >> 8));
            emit_byte(compiler, (uint8_t)(name_idx & 0xFF));
            emit_byte(compiler, (uint8_t)(method_name_idx >> 8));
            emit_byte(compiler, (uint8_t)(method_name_idx & 0xFF));
        }
    }

    if (!has_init) {
        bool needs_init = false;
        for (size_t i = 0; i < cls->member_count; i++) {
            if (cls->members[i]->member_type == KAST_MEMBER_PROPERTY && 
                cls->members[i]->init_value != NULL) {
                needs_init = true;
                break;
            }
        }
        
        if (needs_init) {
            int jmp_patch = emit_jump(compiler, KOP_JMP, 0);
            uint32_t start_addr = compiler->chunk->count;
            
            Local saved_locals[256];
            int saved_local_count = compiler->local_count;
            int saved_scope_depth = compiler->scope_depth;
            int saved_reg_count = compiler->current_reg_count;
            if (saved_local_count > 0) memcpy(saved_locals, compiler->locals, saved_local_count * sizeof(Local));

            compiler->local_count = 0;
            compiler->scope_depth = 1;
            compiler->current_reg_count = 0;
            
            add_local(compiler, "self");
            
            compile_field_initializers(compiler, cls);
            
            emit_byte(compiler, KOP_RET);
            emit_byte(compiler, 0); emit_byte(compiler, 0); emit_byte(compiler, 0);
            
            compiler->local_count = saved_local_count;
            compiler->scope_depth = saved_scope_depth;
            compiler->current_reg_count = saved_reg_count;
            if (saved_local_count > 0) memcpy(compiler->locals, saved_locals, saved_local_count * sizeof(Local));
            
            patch_jump(compiler, jmp_patch, compiler->chunk->count);
             
             int method_name_idx = add_string_constant(compiler, "_init");
             
             emit_byte(compiler, KOP_FUNCTION);
            emit_byte(compiler, (uint8_t)(method_name_idx >> 8));
            emit_byte(compiler, (uint8_t)(method_name_idx & 0xFF));
            emit_byte(compiler, (uint8_t)(start_addr >> 16));
            emit_byte(compiler, (uint8_t)(start_addr >> 8));
            emit_byte(compiler, (uint8_t)(start_addr & 0xFF));
            emit_byte(compiler, 1); // arg_count = 1 (self)
            emit_byte(compiler, 0); // Access: Public
            
            emit_byte(compiler, KOP_METHOD);
            emit_byte(compiler, (uint8_t)(name_idx >> 8));
            emit_byte(compiler, (uint8_t)(name_idx & 0xFF));
            emit_byte(compiler, (uint8_t)(method_name_idx >> 8));
            emit_byte(compiler, (uint8_t)(method_name_idx & 0xFF));
        }
    }
    
    compiler->current_class_name = prev_class_name;
}

static void compile_statement(CompilerState* compiler, KastStatement* stmt) {
    if (!stmt) return;
    switch (stmt->base.type) {
        case KAST_NODE_IMPORT: {
            KastImport* imp = (KastImport*)stmt;
            if (imp->part_count == 0) return;
            
            // Build full dotted path for IMPORT instruction
            int total_len = 0;
            for (size_t i = 0; i < imp->part_count; i++) {
                total_len += strlen(imp->path_parts[i]) + 1; // +1 for dot or null
            }
            char* full_path = (char*)malloc(total_len);
            full_path[0] = '\0';
            for (size_t i = 0; i < imp->part_count; i++) {
                strcat(full_path, imp->path_parts[i]);
                if (i < imp->part_count - 1) strcat(full_path, ".");
            }
            
            int idx = add_string_constant(compiler, full_path);
            int reg = compiler->current_reg_count++; // Temp reg
            
            // IMPORT Rd, NameIdx
            emit_byte(compiler, KOP_IMPORT);
            emit_byte(compiler, reg);
            emit_byte(compiler, (uint8_t)(idx >> 8));
            emit_byte(compiler, (uint8_t)(idx & 0xFF));
            
            // Determine the name to bind in global scope
            // For `import os.print`, we bind `print`.
            // For `import os`, we bind `os`.
            // Usually the last part is the name unless alias is used (AST has alias field but we check logic)
            
            char* bind_name = imp->alias ? imp->alias : imp->path_parts[imp->part_count - 1];
            int bind_idx = add_string_constant(compiler, bind_name);
            
            // SET_GLOBAL Reg, NameIdx
            emit_byte(compiler, KOP_SET_GLOBAL);
            emit_byte(compiler, reg);
            emit_byte(compiler, (uint8_t)(bind_idx >> 8));
            emit_byte(compiler, (uint8_t)(bind_idx & 0xFF));
            
            compiler->current_reg_count--; // Free reg
            free(full_path);
            break;
        }
        case KAST_NODE_VAR_DECL: {
            KastVarDecl* decl = (KastVarDecl*)stmt;
            if (compiler->scope_depth > 0) {
                add_local(compiler, decl->name);
                int reg = resolve_local(compiler, decl->name);
                if (decl->init_value) {
                    compile_expression(compiler, (KastExpression*)decl->init_value, reg);
                }
            } else {
                int reg = compiler->current_reg_count++;
                if (decl->init_value) {
                    compile_expression(compiler, (KastExpression*)decl->init_value, reg);
                }
                int idx = add_string_constant(compiler, decl->name);
                emit_byte(compiler, KOP_SET_GLOBAL);
                emit_byte(compiler, reg);
                emit_byte(compiler, (uint8_t)(idx >> 8));
                emit_byte(compiler, (uint8_t)(idx & 0xFF));
                compiler->current_reg_count--;
            }
            break;
        }
        case KAST_NODE_RETURN: {
            KastReturn* ret = (KastReturn*)stmt;
            if (ret->value) {
                int res_reg = compiler->current_reg_count++;
                compile_expression(compiler, (KastExpression*)ret->value, res_reg);
                if (res_reg != 0) {
                     emit_instruction(compiler, KOP_LOAD, 0, res_reg, 0);
                }
                compiler->current_reg_count--;
            }
            emit_byte(compiler, KOP_RET);
            emit_byte(compiler, 0); emit_byte(compiler, 0); emit_byte(compiler, 0);
            break;
        }
        case KAST_NODE_CALL:
        case KAST_NODE_ASSIGNMENT:
        case KAST_NODE_BINARY_OP:
        case KAST_NODE_MEMBER_ACCESS: {
            int reg = compiler->current_reg_count++;
            compile_expression(compiler, (KastExpression*)stmt, reg);
            compiler->current_reg_count--;
            break;
        }
        case KAST_NODE_TRY_CATCH: {
            KastTryCatch* try_catch = (KastTryCatch*)stmt;
            
            int try_instr = emit_jump(compiler, KOP_TRY, 0);
            
            compile_statement(compiler, (KastStatement*)try_catch->try_block);
            
            emit_byte(compiler, KOP_ENDTRY);
            int jump_over_catches = emit_jump(compiler, KOP_JMP, 0);
            
            patch_jump(compiler, try_instr, compiler->chunk->count);
            
            int ex_reg = compiler->current_reg_count++;
            emit_byte(compiler, KOP_GETEXCEPTION);
            emit_byte(compiler, ex_reg);
            
            int exit_jumps[16];
            int exit_jump_count = 0;
            
            for (size_t i = 0; i < try_catch->catch_count; i++) {
                KorelinCatchBlock* catch_block = &try_catch->catch_blocks[i];
                
                int type_name_idx = add_string_constant(compiler, catch_block->error_type);
                
                int class_reg = compiler->current_reg_count++;
                emit_byte(compiler, KOP_GET_GLOBAL);
                emit_byte(compiler, class_reg);
                emit_byte(compiler, (uint8_t)(type_name_idx >> 8));
                emit_byte(compiler, (uint8_t)(type_name_idx & 0xFF));
                
                int result_reg = compiler->current_reg_count++;
                emit_instruction(compiler, KOP_INSTANCEOF, result_reg, ex_reg, class_reg);
                
                int jump_next = emit_jump(compiler, KOP_JZ, result_reg);
                
                compiler->current_reg_count -= 2; 
                
                // Bind exception variable if present
                if (catch_block->variable_name) {
                    add_local(compiler, catch_block->variable_name);
                    // Copy ex_reg to local_reg using KOP_MOVE
                    int local_reg = compiler->locals[compiler->local_count - 1].reg_index;
                    emit_byte(compiler, KOP_MOVE);
                    emit_byte(compiler, local_reg);
                    emit_byte(compiler, ex_reg);
                    emit_byte(compiler, 0); // Padding
                }
                
                compile_statement(compiler, (KastStatement*)catch_block->body);
                
                if (catch_block->variable_name) {
                    compiler->local_count--;
                    compiler->current_reg_count--; // Free local register
                }
                
                if (exit_jump_count < 16) {
                    exit_jumps[exit_jump_count++] = emit_jump(compiler, KOP_JMP, 0);
                }
                
                patch_jump(compiler, jump_next, compiler->chunk->count);
            }
            
            emit_byte(compiler, KOP_THROW);
            emit_byte(compiler, ex_reg);
            
            compiler->current_reg_count--; 
            
            int end_offset = compiler->chunk->count;
            patch_jump(compiler, jump_over_catches, end_offset);
            for (int i = 0; i < exit_jump_count; i++) {
                patch_jump(compiler, exit_jumps[i], end_offset);
            }
            break;
        }
        case KAST_NODE_THROW: {
            KastThrow* thr = (KastThrow*)stmt;
            int reg = compiler->current_reg_count++;
            compile_expression(compiler, (KastExpression*)thr->value, reg);
            emit_byte(compiler, KOP_THROW);
            emit_byte(compiler, reg);
            compiler->current_reg_count--;
            break;
        }
        case KAST_NODE_BLOCK: {
            KastBlock* block = (KastBlock*)stmt;
            for (size_t i = 0; i < block->statement_count; i++) {
                compile_statement(compiler, block->statements[i]);
            }
            break;
        }
        case KAST_NODE_FUNCTION_DECL: {
            compile_function_decl(compiler, (KastFunctionDecl*)stmt);
            break;
        }
        case KAST_NODE_CLASS_DECL: {
            compile_class_decl(compiler, (KastClassDecl*)stmt);
            break;
        }
        case KAST_NODE_IF: {
             KastIf* kif = (KastIf*)stmt;
             int reg = compiler->current_reg_count++;
             compile_expression(compiler, (KastExpression*)kif->condition, reg);
             int jump_else = emit_jump(compiler, KOP_JZ, reg);
             compile_statement(compiler, (KastStatement*)kif->then_branch);
             
             if (kif->else_branch) {
                 int jump_end = emit_jump(compiler, KOP_JMP, 0);
                 patch_jump(compiler, jump_else, compiler->chunk->count);
                 compile_statement(compiler, (KastStatement*)kif->else_branch);
                 patch_jump(compiler, jump_end, compiler->chunk->count);
             } else {
                 patch_jump(compiler, jump_else, compiler->chunk->count);
             }
             compiler->current_reg_count--;
             break;
        }
        case KAST_NODE_WHILE: {
            KastWhile* kwhile = (KastWhile*)stmt;
            
            int loop_start = compiler->chunk->count;
            enter_loop(compiler, loop_start);

            int cond_reg = compiler->current_reg_count++;
            
            // Compile condition
            compile_expression(compiler, (KastExpression*)kwhile->condition, cond_reg);
            
            // Jump if false
            int jump_exit = emit_jump(compiler, KOP_JZ, cond_reg);
            compiler->current_reg_count--; // Free cond_reg
            
            // Compile body
            compile_statement(compiler, kwhile->body);
            
            // Jump back to start
            int jump_loop = emit_jump(compiler, KOP_JMP, 0);
            patch_jump(compiler, jump_loop, loop_start);
            
            // Patch exit
            patch_jump(compiler, jump_exit, compiler->chunk->count);
            
            exit_loop(compiler);
            break;
        }
        case KAST_NODE_DO_WHILE: {
            KastDoWhile* kdo = (KastDoWhile*)stmt;
            
            int loop_start = compiler->chunk->count;
            enter_loop(compiler, -1);
            
            // Compile body
            compile_statement(compiler, kdo->body);
            
            int cond_start = compiler->chunk->count;
            resolve_continue(compiler, cond_start);
            
            // Compile condition
            int cond_reg = compiler->current_reg_count++;
            compile_expression(compiler, (KastExpression*)kdo->condition, cond_reg);
            
            // Jump if true (continue loop)
            int jump_loop = emit_jump(compiler, KOP_JNZ, cond_reg);
            patch_jump(compiler, jump_loop, loop_start);
            
            compiler->current_reg_count--;
            
            exit_loop(compiler);
            break;
        }
        case KAST_NODE_SWITCH: {
            KastSwitch* kswitch = (KastSwitch*)stmt;
            
            // Evaluate Condition
            int val_reg = compiler->current_reg_count++;
            compile_expression(compiler, (KastExpression*)kswitch->condition, val_reg);
            
            // Enter breakable scope
            enter_loop(compiler, -1);
            
            // Prepare jumps
            int* body_jumps = (int*)malloc(kswitch->case_count * sizeof(int));
            
            for (size_t i = 0; i < kswitch->case_count; i++) {
                // Compile Case Value
                int case_reg = compiler->current_reg_count++;
                compile_expression(compiler, (KastExpression*)kswitch->cases[i].value, case_reg);
                
                // Compare (val == case)
                // Note: val_reg is 1st operand, case_reg is 2nd.
                emit_instruction(compiler, KOP_EQ, case_reg, val_reg, case_reg); 
                
                // Jump to Body if Equal (JNZ)
                body_jumps[i] = emit_jump(compiler, KOP_JNZ, case_reg);
                
                compiler->current_reg_count--; // Free case_reg
            }
            
            // Default jump (if no match, go to default)
            int default_jump = emit_jump(compiler, KOP_JMP, 0);
            
            // Compile Bodies
            for (size_t i = 0; i < kswitch->case_count; i++) {
                patch_jump(compiler, body_jumps[i], compiler->chunk->count);
                if (kswitch->cases[i].body) {
                    compile_statement(compiler, kswitch->cases[i].body);
                }
                // Fallthrough is automatic in this structure unless break is used
            }
            
            // Default Body
            patch_jump(compiler, default_jump, compiler->chunk->count);
            if (kswitch->default_branch) {
                compile_statement(compiler, kswitch->default_branch);
            }
            
            // Cleanup
            free(body_jumps);
            compiler->current_reg_count--; // Free val_reg
            
            exit_loop(compiler); // Patches breaks to here
            break;
        }
        case KAST_NODE_FOR: {
            KastFor* kfor = (KastFor*)stmt;
            
            // Enter scope
            compiler->scope_depth++;
            
            // Init
            if (kfor->init) {
                compile_statement(compiler, kfor->init);
            }
            
            int loop_start = compiler->chunk->count;
            enter_loop(compiler, -1);

            int jump_exit = -1;
            
            // Condition
            if (kfor->condition) {
                int cond_reg = compiler->current_reg_count++;
                compile_expression(compiler, (KastExpression*)kfor->condition, cond_reg);
                jump_exit = emit_jump(compiler, KOP_JZ, cond_reg);
                compiler->current_reg_count--;
            }
            
            // Body
            compile_statement(compiler, kfor->body);
            
            int increment_start = compiler->chunk->count;
            resolve_continue(compiler, increment_start);
            
            // Increment
            if (kfor->increment) {
                int temp_reg = compiler->current_reg_count++;
                compile_expression(compiler, (KastExpression*)kfor->increment, temp_reg);
                compiler->current_reg_count--;
            }
            
            // Jump back
            int back_jump_patch = emit_jump(compiler, KOP_JMP, 0);
            patch_jump(compiler, back_jump_patch, loop_start);
            
            // Patch exit
            if (jump_exit != -1) {
                patch_jump(compiler, jump_exit, compiler->chunk->count);
            }
            
            exit_loop(compiler);
            
            // Exit scope
            compiler->scope_depth--;
            while (compiler->local_count > 0 && 
                   compiler->locals[compiler->local_count - 1].depth > compiler->scope_depth) {
                free(compiler->locals[compiler->local_count - 1].name);
                compiler->local_count--;
                compiler->current_reg_count--;
            }
            break;
        }
        case KAST_NODE_BREAK: {
             if (compiler->loop_depth == 0) {
                 printf("Compile Error: 'break' outside of loop\n");
                 return;
             }
             int jmp = emit_jump(compiler, KOP_JMP, 0);
             add_break(compiler, jmp);
             break;
        }
        case KAST_NODE_CONTINUE: {
             if (compiler->loop_depth == 0) {
                 printf("Compile Error: 'continue' outside of loop\n");
                 return;
             }
             int jmp = emit_jump(compiler, KOP_JMP, 0);
             add_continue(compiler, jmp);
             break;
        }
        default: break;
    }
}

int compile_ast(KastProgram* program, KBytecodeChunk* chunk) {
    CompilerState* compiler = (CompilerState*)malloc(sizeof(CompilerState));
    if (!compiler) return 1;
    
    init_compiler(compiler, chunk);
    
    for (int i = 0; i < program->statement_count; i++) {
        compile_statement(compiler, program->statements[i]);
    }
    
    emit_byte(compiler, KOP_RET); 
    emit_byte(compiler, 0); emit_byte(compiler, 0); emit_byte(compiler, 0);
    
    // Cleanup compiler resources if any (locals names are strdup'ed)
    for(int i=0; i<compiler->local_count; i++) {
        free(compiler->locals[i].name);
    }
    free(compiler);
    
    return 0;
}
