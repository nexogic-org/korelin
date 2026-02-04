//
// Created by Helix on 2026/1/21.
//

#include "comeonjit.h"
#include "kvm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

// --- 輔助宏 ---

// REX Prefix fields
#define REX_W 0x48 // 64-bit operand

// x64 Registers
#define RAX 0
#define RCX 1
#define RDX 2
#define RBX 3
#define RSP 4
#define RBP 5
#define RSI 6
#define RDI 7

// KValue Offsets (Based on KVM struct)
// type: offset 0 (4 bytes)
// as: offset 8 (8 bytes)
#define OFFSET_KVALUE_TYPE 0
#define OFFSET_KVALUE_AS   8
#define SIZE_KVALUE        16

// KVM Offsets
// registers is the first field
#define OFFSET_KVM_REGISTERS 0

// --- 內存管理 ---

void jit_init(ComeOnJIT* jit) {
    jit->enabled = true;
    jit->compiled_functions = 0;
    
#ifdef __x86_64__
    jit->arch = JIT_ARCH_X64;
    // printf("[ComeOnJIT] Target: x64 (Aggressive Optimization Enabled)\n");
#elif defined(_M_X64)
    jit->arch = JIT_ARCH_X64;
    // printf("[ComeOnJIT] Target: x64 (Aggressive Optimization Enabled)\n");
#else
    jit->arch = JIT_ARCH_UNKNOWN;
    jit->enabled = false;
    // printf("[ComeOnJIT] Unsupported architecture. JIT disabled.\n");
    return;
#endif

    jit->exec_memory_size = 1024 * 1024 * 4; // 4MB
    jit->exec_memory_used = 0;

#ifdef _WIN32
    jit->exec_memory = (uint8_t*)VirtualAlloc(NULL, jit->exec_memory_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    jit->exec_memory = (uint8_t*)mmap(NULL, jit->exec_memory_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif

    if (jit->exec_memory == NULL) {
        printf("[ComeOnJIT] Failed to allocate executable memory.\n");
        jit->enabled = false;
    }
}

void jit_cleanup(ComeOnJIT* jit) {
    if (jit->exec_memory) {
#ifdef _WIN32
        VirtualFree(jit->exec_memory, 0, MEM_RELEASE);
#else
        munmap(jit->exec_memory, jit->exec_memory_size);
#endif
    }
}

static uint8_t* jit_alloc(ComeOnJIT* jit, size_t size) {
    if (jit->exec_memory_used + size > jit->exec_memory_size) return NULL;
    uint8_t* ptr = jit->exec_memory + jit->exec_memory_used;
    jit->exec_memory_used += size;
    return ptr;
}

// --- x64 Emitter ---

#define EMIT_1(b1) *code++ = (b1)
#define EMIT_2(b1, b2) { *code++ = (b1); *code++ = (b2); }
#define EMIT_3(b1, b2, b3) { *code++ = (b1); *code++ = (b2); *code++ = (b3); }
#define EMIT_4(b1, b2, b3, b4) { *code++ = (b1); *code++ = (b2); *code++ = (b3); *code++ = (b4); }
#define EMIT_INT32(val) { *(int32_t*)code = (val); code += 4; }

// ModR/M helper
// mod: 2 bits, reg: 3 bits, rm: 3 bits
#define MODRM(mod, reg, rm) ((mod << 6) | (reg << 3) | rm)

// Load Register Value to CPU Register
// mov cpu_reg, [vm_base + OFFSET_REGISTERS + vm_reg_idx * 16 + 8]
static void emit_load_reg(uint8_t** ptr, int cpu_reg, int vm_reg_idx, int vm_base_reg) {
    uint8_t* code = *ptr;
    int32_t offset = OFFSET_KVM_REGISTERS + vm_reg_idx * SIZE_KVALUE + OFFSET_KVALUE_AS;
    
    EMIT_1(REX_W); // 64-bit
    EMIT_1(0x8B);  // MOV r64, r/m64
    // ModR/M: [vm_base + disp32]
    EMIT_1(MODRM(2, cpu_reg, vm_base_reg)); 
    EMIT_INT32(offset);
    
    *ptr = code;
}

// Store CPU Register to VM Register
// mov [vm_base + ... + 8], cpu_reg
static void emit_store_reg(uint8_t** ptr, int cpu_reg, int vm_reg_idx, int vm_base_reg) {
    uint8_t* code = *ptr;
    int32_t offset = OFFSET_KVM_REGISTERS + vm_reg_idx * SIZE_KVALUE + OFFSET_KVALUE_AS;
    
    EMIT_1(REX_W);
    EMIT_1(0x89); // MOV r/m64, r64
    EMIT_1(MODRM(2, cpu_reg, vm_base_reg));
    EMIT_INT32(offset);
    
    *ptr = code;
}

// Set VM Register Type to INT
// mov dword ptr [vm_base + ...], VAL_INT
static void emit_set_type_int(uint8_t** ptr, int vm_reg_idx, int vm_base_reg) {
    uint8_t* code = *ptr;
    int32_t offset = OFFSET_KVM_REGISTERS + vm_reg_idx * SIZE_KVALUE + OFFSET_KVALUE_TYPE;
    
    // MOV r/m32, imm32 (C7 /0 id)
    EMIT_1(0xC7);
    EMIT_1(MODRM(2, 0, vm_base_reg));
    EMIT_INT32(offset);
    EMIT_INT32(VAL_INT);
    
    *ptr = code;
}

// --- JIT Compilation ---

// Jump Fixup Structure
typedef struct {
    int jump_inst_offset; // Offset in machine code where the relative jump starts
    int target_bytecode_offset; // Target bytecode index
} JumpFixup;

void* jit_compile(ComeOnJIT* jit, KBytecodeChunk* chunk) {
    if (!jit->enabled || jit->arch != JIT_ARCH_X64) return NULL;

    size_t max_size = chunk->count * 64 + 1024; // Conservative estimate
    uint8_t* start_addr = jit_alloc(jit, max_size);
    if (!start_addr) return NULL;

    uint8_t* code = start_addr;
    
    // Map bytecode offset to machine code offset
    // -1 means not yet generated
    int* bc_to_mc = (int*)malloc(chunk->count * sizeof(int));
    for(size_t i=0; i<chunk->count; i++) bc_to_mc[i] = -1;

    // Jump fixups
    JumpFixup fixups[256]; // Simple fixed size for demo
    int fixup_count = 0;

    // Determine VM pointer register
#ifdef _WIN32
    int vm_reg = RCX; // Windows: 1st arg in RCX
#else
    int vm_reg = RDI; // SysV: 1st arg in RDI
#endif

    // Prologue
    EMIT_1(0x55); // push rbp
    EMIT_3(0x48, 0x89, 0xE5); // mov rbp, rsp
    EMIT_1(0x53); // push rbx (callee saved)

    uint8_t* ip = chunk->code;
    uint8_t* end = chunk->code + chunk->count;

    while (ip < end) {
        int bc_offset = (int)(ip - chunk->code);
        bc_to_mc[bc_offset] = (int)(code - start_addr);

        uint8_t opcode = *ip++;

        switch (opcode) {
            case KOP_HALT:
                EMIT_1(0xB8); EMIT_INT32(0); // mov eax, 0
                EMIT_1(0x5B); // pop rbx
                EMIT_1(0xC9); // leave
                EMIT_1(0xC3); // ret
                break;

            case KOP_ADDI: { // ADDI Rd, Ra, Imm
                uint8_t rd = *ip++;
                uint8_t ra = *ip++;
                int8_t imm = (int8_t)(*ip++);
                
                // Load Ra -> RAX
                emit_load_reg(&code, RAX, ra, vm_reg);
                // ADD RAX, Imm
                EMIT_1(REX_W); EMIT_1(0x83); EMIT_1(0xC0); EMIT_1((uint8_t)imm);
                // Store RAX -> Rd
                emit_store_reg(&code, RAX, rd, vm_reg);
                // Set Type
                emit_set_type_int(&code, rd, vm_reg);
                break;
            }

            case KOP_ADD: { // ADD Rd, Ra, Rb
                uint8_t rd = *ip++;
                uint8_t ra = *ip++;
                uint8_t rb = *ip++;

                emit_load_reg(&code, RAX, ra, vm_reg);
                emit_load_reg(&code, RBX, rb, vm_reg); // Use RBX as temp
                
                // ADD RAX, RBX
                EMIT_3(REX_W, 0x01, 0xD8); 
                
                emit_store_reg(&code, RAX, rd, vm_reg);
                emit_set_type_int(&code, rd, vm_reg);
                break;
            }
            
            case KOP_SUB: {
                uint8_t rd = *ip++;
                uint8_t ra = *ip++;
                uint8_t rb = *ip++;
                emit_load_reg(&code, RAX, ra, vm_reg);
                emit_load_reg(&code, RBX, rb, vm_reg);
                // SUB RAX, RBX
                EMIT_3(REX_W, 0x29, 0xD8);
                emit_store_reg(&code, RAX, rd, vm_reg);
                emit_set_type_int(&code, rd, vm_reg);
                break;
            }

            case KOP_MUL: {
                uint8_t rd = *ip++;
                uint8_t ra = *ip++;
                uint8_t rb = *ip++;
                emit_load_reg(&code, RAX, ra, vm_reg);
                emit_load_reg(&code, RBX, rb, vm_reg);
                // IMUL RAX, RBX
                EMIT_4(REX_W, 0x0F, 0xAF, 0xC3);
                emit_store_reg(&code, RAX, rd, vm_reg);
                emit_set_type_int(&code, rd, vm_reg);
                break;
            }
            
            case KOP_DIV: {
                uint8_t rd = *ip++;
                uint8_t ra = *ip++;
                uint8_t rb = *ip++;
                emit_load_reg(&code, RAX, ra, vm_reg); // Dividend
                emit_load_reg(&code, RBX, rb, vm_reg); // Divisor
                // CQO (Sign extend RAX to RDX:RAX)
                EMIT_2(REX_W, 0x99);
                // IDIV RBX
                EMIT_3(REX_W, 0xF7, 0xFB);
                emit_store_reg(&code, RAX, rd, vm_reg);
                emit_set_type_int(&code, rd, vm_reg);
                break;
            }

            case KOP_JMP: { // JMP Imm24 (Absolute bytecode offset)
                uint8_t b1 = *ip++; uint8_t b2 = *ip++; uint8_t b3 = *ip++;
                int target = (b1 << 16) | (b2 << 8) | b3;
                
                // JMP rel32 (E9 cd)
                EMIT_1(0xE9);
                // Record fixup
                fixups[fixup_count].jump_inst_offset = (int)(code - start_addr);
                fixups[fixup_count].target_bytecode_offset = target;
                fixup_count++;
                EMIT_INT32(0); // Placeholder
                break;
            }
            
            case KOP_JZ: { // JZ Ra, Imm16 (Relative bytecode offset)
                uint8_t ra = *ip++;
                uint8_t b1 = *ip++; uint8_t b2 = *ip++;
                int16_t offset = (int16_t)((b1 << 8) | b2);
                int target = bc_offset + 3 + offset; // Current inst start + 3 + offset ?? 
                // Wait, logic in interpreter: ip += offset. ip is AFTER reading args.
                // So target = (ip index) + offset. ip index is bc_offset + 4.
                target = (bc_offset + 4) + offset;

                emit_load_reg(&code, RAX, ra, vm_reg);
                // TEST RAX, RAX
                EMIT_3(REX_W, 0x85, 0xC0);
                
                // JZ rel32 (0F 84 cd)
                EMIT_2(0x0F, 0x84);
                fixups[fixup_count].jump_inst_offset = (int)(code - start_addr);
                fixups[fixup_count].target_bytecode_offset = target;
                fixup_count++;
                EMIT_INT32(0);
                break;
            }
            
            case KOP_JNZ: { // JNZ Ra, Imm16
                uint8_t ra = *ip++;
                uint8_t b1 = *ip++; uint8_t b2 = *ip++;
                int16_t offset = (int16_t)((b1 << 8) | b2);
                int target = (bc_offset + 4) + offset;

                emit_load_reg(&code, RAX, ra, vm_reg);
                EMIT_3(REX_W, 0x85, 0xC0);
                
                // JNZ rel32 (0F 85 cd)
                EMIT_2(0x0F, 0x85);
                fixups[fixup_count].jump_inst_offset = (int)(code - start_addr);
                fixups[fixup_count].target_bytecode_offset = target;
                fixup_count++;
                EMIT_INT32(0);
                break;
            }

            case KOP_XOR: {
                uint8_t rd = *ip++; uint8_t ra = *ip++; uint8_t rb = *ip++;
                emit_load_reg(&code, RAX, ra, vm_reg);
                emit_load_reg(&code, RBX, rb, vm_reg);
                EMIT_3(REX_W, 0x31, 0xD8); // XOR RAX, RBX
                emit_store_reg(&code, RAX, rd, vm_reg);
                emit_set_type_int(&code, rd, vm_reg);
                break;
            }

            default:
                // Fallback / unsupported
                // Generate a return -1 to signal error?
                // For now, just HALT
                EMIT_1(0xB8); EMIT_INT32(0); // mov eax, 0
                EMIT_1(0x5B); // pop rbx
                EMIT_1(0xC9); // leave
                EMIT_1(0xC3); // ret
                goto finish_compilation;
        }
    }

finish_compilation:
    // Resolve Fixups
    for (int i = 0; i < fixup_count; i++) {
        int jump_src = fixups[i].jump_inst_offset; // This points to the 4-byte immediate
        int target_bc = fixups[i].target_bytecode_offset;
        
        if (target_bc >= 0 && target_bc < chunk->count && bc_to_mc[target_bc] != -1) {
            int target_mc = bc_to_mc[target_bc];
            // Calculate relative offset: target - (src + 4)
            int rel_offset = target_mc - (jump_src + 4);
            
            // Patch code
            *(int32_t*)(start_addr + jump_src) = rel_offset;
        } else {
            // printf("[ComeOnJIT] Failed to resolve jump target %d\n", target_bc);
        }
    }

    free(bc_to_mc);

#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), start_addr, code - start_addr);
#endif

    jit->compiled_functions++;
    return (void*)start_addr;
}
