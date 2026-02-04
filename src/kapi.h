#ifndef KORELIN_KAPI_H
#define KORELIN_KAPI_H

#include "kvm.h" // Needed for KVM*

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// --- 基本類型定義 ---

typedef int64_t KInt;
typedef bool KBool;
typedef double KFloat;
typedef const char* KString;

// --- 核心 API ---

// 初始化 Korelin 環境 (可選，KRun 會自動調用)
void KInit();

void KBindVM(KVM* vm); // Bind existing VM to API

// 銷毀 Korelin 環境
void KCleanup();

// 運行 Korelin 代碼字符串
void KRun(const char* source);

// --- 庫管理 ---

// 註冊一個新包 (庫)
void KLibNew(const char* package_name);

// 向包中添加成員
// type: "function", "var", "const", "subpackage", "class"
// value: 函數指針或變量指針
void KLibAdd(const char* package_name, const char* type, const char* name, void* value);

// 注册类
void KLibNewClass(const char* class_name);

// 向类添加方法
void KLibAddMethod(const char* class_name, const char* method_name, void* func);

// 注册全局函数
void KLibAddGlobal(const char* name, void* func);

// --- 類管理 ---

typedef enum {
    KMOD_PUBLIC = 0,
    KMOD_PRIVATE = 1,
    KMOD_PROTECTED = 2,
    KMOD_STATIC = 4
} KModifier;

// 向類中添加成員 (方法或屬性)
// member_type: "method", "field"
// modifier: KModifier 標誌位 (e.g. KMOD_PUBLIC | KMOD_STATIC)
void KLibClassAdd(const char* class_name, const char* member_type, const char* name, void* value, int modifier);

// --- 函數實現輔助 ---

// 在 Native 函數中設置返回值
void KReturnInt(KInt val);
void KReturnBool(KBool val);
void KReturnFloat(KFloat val);
void KReturnString(KString val);
void KReturnVoid();

// 獲取參數 (用於手動綁定模式)
int KGetArgCount();
KInt KGetArgInt(int index);
KBool KGetArgBool(int index);
KFloat KGetArgFloat(int index);
KString KGetArgString(int index);

#ifdef __cplusplus
}
#endif

#endif // KORELIN_KAPI_H
