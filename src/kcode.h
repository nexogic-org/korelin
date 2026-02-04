//
// Created by Helix on 2026/1/10.
//

#ifndef KORELIN_KCODE_H
#define KORELIN_KCODE_H

#include <stdint.h>
#include <stddef.h>
#include "kparser.h"

// 基於 doc/字節碼理論.md 的操作碼定義
typedef enum {
    // 2.1 算術與邏輯 (0x00-0x1F)
    KOP_ADD = 0x00, KOP_SUB = 0x01, KOP_MUL = 0x02, KOP_MULH = 0x03,
    KOP_DIV = 0x04, KOP_UDIV = 0x05, KOP_MOD = 0x06, KOP_UMOD = 0x07,
    KOP_NEG = 0x08, KOP_ABS = 0x09,

    // 整數比較 (新增)
    KOP_EQ = 0x0A, KOP_NE = 0x0B, 
    KOP_LT = 0x0C, KOP_LE = 0x0D, 
    KOP_GT = 0x0E, KOP_GE = 0x0F,

    KOP_ADDI = 0x10, KOP_SUBI = 0x11, KOP_MULI = 0x12, KOP_DIVI = 0x13, KOP_MODI = 0x14,
    KOP_LDI = 0x15, // LDI Rd, Imm8 (Load Immediate Integer)
    KOP_LDI64 = 0x16, // LDI64 Rd, Imm64 (Load Immediate Integer 64bit)
    KOP_LDB = 0x17,   // LDB Rd, Imm8 (Load Bool)

    KOP_AND = 0x18, KOP_OR = 0x19, KOP_XOR = 0x1A, KOP_NOT = 0x1B,
    KOP_ANDN = 0x1C, KOP_ORN = 0x1D, KOP_XORN = 0x1E, KOP_TEST = 0x1F,

    // 移位運算 (0x20-0x2F)
    KOP_SHL = 0x20, KOP_SHR = 0x21, KOP_SAR = 0x22, KOP_ROL = 0x23, KOP_ROR = 0x24,
    KOP_SHLI = 0x25, KOP_SHRI = 0x26, KOP_SARI = 0x27,

    // 位運算 (0x28-0x2F)
    KOP_CLZ = 0x28, KOP_CTZ = 0x29, KOP_POPCNT = 0x2A, KOP_REV = 0x2B,
    KOP_BEXT = 0x2C, KOP_BDEP = 0x2D, KOP_BGRP = 0x2E, KOP_RORI = 0x2F,

    // 2.2 浮點數 (0x30-0x4F)
    KOP_FADD_S = 0x30, KOP_FSUB_S = 0x31, KOP_FMUL_S = 0x32, KOP_FDIV_S = 0x33,
    KOP_FMADD_S = 0x34, KOP_FMSUB_S = 0x35, KOP_FNMADD_S = 0x36, KOP_FNMSUB_S = 0x37,

    KOP_FADD_D = 0x38, KOP_FSUB_D = 0x39, KOP_FMUL_D = 0x3A, KOP_FDIV_D = 0x3B,
    KOP_FMADD_D = 0x3C, KOP_FMSUB_D = 0x3D, KOP_FSQRT_D = 0x3E, KOP_FRSQRT_D = 0x3F,

    KOP_FCVT_S_D = 0x40, KOP_FCVT_D_S = 0x41, KOP_FCVT_W_S = 0x42, KOP_FCVT_W_D = 0x43,
    KOP_FCVT_S_W = 0x44, KOP_FCVT_D_W = 0x45, KOP_FCMP_S = 0x46, KOP_FCMP_D = 0x47,
    KOP_FEQ_S = 0x48, KOP_FLT_S = 0x49, KOP_FLE_S = 0x4A,
    KOP_FEQ_D = 0x4B, KOP_FLT_D = 0x4C, KOP_FLE_D = 0x4D,
    KOP_FSGNJ_S = 0x4E, KOP_FSGNJ_D = 0x4F,

    // 2.3 內存 (0x50-0x6F)
    KOP_LOAD = 0x50, KOP_LOAD_U = 0x51, KOP_LOAD32 = 0x52, KOP_LOAD32S = 0x53,
    KOP_LOAD16 = 0x54, KOP_LOAD16S = 0x55, KOP_LOAD8 = 0x56, KOP_LOAD8S = 0x57,

    KOP_STORE = 0x58, KOP_STORE32 = 0x59, KOP_STORE16 = 0x5A, KOP_STORE8 = 0x5B,

    KOP_PUSH = 0x5C, KOP_POP = 0x5D, KOP_PUSH_R = 0x5E, KOP_POP_R = 0x5F,
    KOP_ENTER = 0x60, KOP_LEAVE = 0x61,

    KOP_LEA = 0x62, KOP_LEA_G = 0x63, KOP_LEA_INDEX = 0x64,

    // 原子操作 (0x65-0x6F)
    KOP_ATOMIC_LOAD = 0x65, KOP_ATOMIC_STORE = 0x66,
    KOP_ATOMIC_ADD = 0x67, KOP_ATOMIC_SUB = 0x68,
    KOP_ATOMIC_AND = 0x69, KOP_ATOMIC_OR = 0x6A, KOP_ATOMIC_XOR = 0x6B,
    KOP_ATOMIC_SWAP = 0x6C, KOP_ATOMIC_CAS = 0x6D,
    KOP_MEMBAR = 0x6E, KOP_PREFETCH = 0x6F,

    // 2.4 控制流 (0x70-0x8F)
    KOP_JMP = 0x70, KOP_JMPR = 0x71, KOP_JREL = 0x72,

    KOP_JEQ = 0x73, KOP_JNE = 0x74, KOP_JGT = 0x75, KOP_JGE = 0x76,
    KOP_JLT = 0x77, KOP_JLE = 0x78, KOP_JGTU = 0x79, KOP_JGEU = 0x7A,
    KOP_JLTU = 0x7B, KOP_JLEU = 0x7C,

    KOP_CALL = 0x7D, KOP_CALLR = 0x7E, KOP_RET = 0x7F,
    KOP_TAILCALL = 0x80, KOP_TAILCALLR = 0x81,

    KOP_JZ = 0x82, KOP_JNZ = 0x83, KOP_JS = 0x84, KOP_JNS = 0x85,
    KOP_JO = 0x86, KOP_JNO = 0x87, KOP_JC = 0x88, KOP_JNC = 0x89,

    KOP_CMOVE = 0x8A, KOP_CMOVNE = 0x8B, KOP_CMOVG = 0x8C, KOP_CMOVGE = 0x8D,
    KOP_CMOVL = 0x8E, KOP_CMOVLE = 0x8F,

    // 2.5 對象 (0x90-0xAF)
    KOP_NEW = 0x90, KOP_NEWA = 0x91, KOP_NEWM = 0x92, KOP_DEL = 0x93, KOP_DELA = 0x94,

    KOP_GETF = 0x95, KOP_PUTF = 0x96, KOP_GETS = 0x97, KOP_PUTS = 0x98,
    KOP_GETFA = 0x99, KOP_PUTFA = 0x9A, KOP_ARRAYLEN = 0x9B,

    KOP_CLASS = 0x9C, KOP_METHOD = 0x9D, 
    KOP_FUNCTION = 0x9E, // Create function object
    
    KOP_INVOKE = 0x9F, KOP_INVOKESPECIAL = 0xA0, KOP_INVOKESTATIC = 0xA1,
    KOP_INVOKEINTERFACE = 0xA2, KOP_INVOKEDYNAMIC = 0xA3,

    KOP_CAST = 0xAC, KOP_CHECKCAST = 0xAD, KOP_INSTANCEOF = 0xAE,
    KOP_GETCLASS = 0xA4, KOP_GETSUPER = 0xA5, KOP_GETINTERFACES = 0xA6,

    KOP_GETFIELDID = 0xA7, KOP_GETMETHODID = 0xA8, KOP_GETCONSTRUCTOR = 0xA9,
    KOP_GETANNOTATION = 0xAA, KOP_SETACCESSIBLE = 0xAB,

    KOP_MOVE = 0xB4, KOP_LDN = 0xB5, KOP_INHERIT = 0xB6,

    // 2.6 模塊 (0xB0-0xCF)
    KOP_IMPORT = 0xB0, KOP_EXPORT = 0xB1, KOP_OPEN = 0xB2, KOP_CLOSE = 0xB3,
    // KOP_REQUIRES = 0xB4, KOP_PROVIDES = 0xB5, KOP_USES = 0xB6, // Overwritten
    KOP_REQUIRES = 0xC7, KOP_PROVIDES = 0xC8, KOP_USES = 0xC9, // Moved

    KOP_LDC = 0xB7, KOP_LDS = 0xB8, KOP_LDCF = 0xB9, KOP_LDCD = 0xBA,
    KOP_LDCW = 0xBB, KOP_LDCMP = 0xBC,

    KOP_PACKAGE = 0xBD, KOP_IMPORT_PKG = 0xBE, KOP_EXPORT_PKG = 0xBF, KOP_OPENS = 0xC0,

    KOP_LOADSERVICE = 0xC1, KOP_FINDSERVICE = 0xC2,
    KOP_INSTALLSERVICE = 0xC3, KOP_REMOVESERVICE = 0xC4,
    
    KOP_GET_GLOBAL = 0xC5, // GET_GLOBAL Rd, StringIndex
    KOP_SET_GLOBAL = 0xC6, // SET_GLOBAL Ra, StringIndex

    // 2.7 異常 (0xD0-0xDF)
    KOP_THROW = 0xD0, KOP_THROWS = 0xD1, KOP_RETHROW = 0xD2, KOP_THROWU = 0xD3,
    KOP_TRY = 0xD4, KOP_CATCH = 0xD5, KOP_FINALLY = 0xD6, KOP_ENDTRY = 0xD7, KOP_CATCHALL = 0xD8,

    KOP_GETEXCEPTION = 0xD9, KOP_CLEAREXCEPTION = 0xDA, KOP_SETSTACKTRACE = 0xDB,
    KOP_GETSTACKTRACE = 0xDC, KOP_GETCAUSE = 0xDD, KOP_GETMESSAGE = 0xDE,
    KOP_FILLINSTACKTRACE = 0xDF,

    // 2.8 同步 (0xE0-0xEF)
    KOP_MONITORENTER = 0xE0, KOP_MONITOREXIT = 0xE1, KOP_TRYMONITORENTER = 0xE2,
    KOP_LOCK = 0xE3, KOP_UNLOCK = 0xE4, KOP_TRYLOCK = 0xE5,

    KOP_SYNCMETHOD = 0xE6, KOP_SYNCDECLARE = 0xE7, KOP_SYNCBLOCK = 0xE8, KOP_ENDSYNC = 0xE9,

    KOP_WAIT = 0xEA, KOP_WAITN = 0xEB, KOP_NOTIFY = 0xEC, KOP_NOTIFYALL = 0xED,
    KOP_AWAITSIGNAL = 0xEE, KOP_SIGNAL = 0xEF,

    // 2.9 系統 (0xF0-0xFF)
    KOP_SYSCALL = 0xF0, KOP_BREAK = 0xF1, KOP_TRAP = 0xF2, KOP_DEBUG = 0xF3,
    KOP_PROFILE = 0xF4, KOP_TRACE = 0xF5,

    KOP_HALT = 0xF6, KOP_GC = 0xF7, KOP_GCINFO = 0xF8, KOP_HEAPINFO = 0xF9,
    KOP_THREADINFO = 0xFA, KOP_STACKINFO = 0xFB,

    KOP_ALLOC = 0xFC, KOP_FREE = 0xFD, KOP_RESIZE = 0xFE, KOP_MEMINFO = 0xFF

} KOpcodes;

// 字節碼容器
typedef struct {
    uint8_t* code;
    size_t capacity;
    size_t count;

    // 常量池（目前簡化版）
    // 在實際實現中，這將存儲字符串、數字、類引用等。
    // 對於 v1.0，我們可能只是嵌入立即數或擁有一個簡單的字符串表
    char** string_table;
    size_t string_count;
    
    // 用於調試的行號映射
    int* lines;
    
    // JIT Cache
    void* jit_code; // 指向編譯後的機器碼
    
    // Debug info
    char* filename;
} KBytecodeChunk;

// 編譯器/生成器狀態
typedef struct {
    KBytecodeChunk* chunk;
    Parser* parser; // 用於錯誤報告
    // 符號表 / 寄存器分配信息可以放在這裡
    int current_register; // 簡單的寄存器計數器
} KCompiler;

// API
void init_chunk(KBytecodeChunk* chunk);
void free_chunk(KBytecodeChunk* chunk);
void write_chunk(KBytecodeChunk* chunk, uint8_t byte, int line);

// 高層生成
// 成功返回 0，錯誤返回非零
int compile_ast(KastProgram* program, KBytecodeChunk* chunk);

#endif //KORELIN_KCODE_H
