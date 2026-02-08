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

/**
 * @brief 初始化 Korelin 環境 (可選，KRun 會自動調用)
 */
void KInit();

/**
 * @brief 綁定現有 VM 到 API
 */
void KBindVM(KVM* vm);

/**
 * @brief 銷毀 Korelin 環境
 */
void KCleanup();

/**
 * @brief 運行 Korelin 代碼字符串
 */
void KRun(const char* source);

// --- 庫管理 ---

/**
 * @brief 註冊一個新包 (庫)
 */
void KLibNew(const char* package_name);

/**
 * @brief 向包中添加成員
 * @param type "function", "var", "const", "subpackage", "class"
 * @param value 函數指針或變量指針
 */
void KLibAdd(const char* package_name, const char* type, const char* name, void* value);

/**
 * @brief 註冊類
 */
void KLibNewClass(const char* class_name);

/**
 * @brief 向類添加方法
 */
void KLibAddMethod(const char* class_name, const char* method_name, void* func);

/**
 * @brief 註冊全局函數
 */
void KLibAddGlobal(const char* name, void* func);

// --- 類管理 ---

/**
 * @brief 修飾符枚舉
 */
typedef enum {
    KMOD_PUBLIC = 0,
    KMOD_PRIVATE = 1,
    KMOD_PROTECTED = 2,
    KMOD_STATIC = 4
} KModifier;

/**
 * @brief 向類中添加成員 (方法或屬性)
 * @param member_type "method", "field"
 * @param modifier KModifier 標誌位 (e.g. KMOD_PUBLIC | KMOD_STATIC)
 */
void KLibClassAdd(const char* class_name, const char* member_type, const char* name, void* value, int modifier);

// --- 函數實現輔助 ---

/**
 * @brief 在 Native 函數中設置返回值 (整數)
 */
void KReturnInt(KInt val);

/**
 * @brief 在 Native 函數中設置返回值 (布爾值)
 */
void KReturnBool(KBool val);

/**
 * @brief 在 Native 函數中設置返回值 (浮點數)
 */
void KReturnFloat(KFloat val);

/**
 * @brief 在 Native 函數中設置返回值 (字符串)
 */
void KReturnString(KString val);

/**
 * @brief 在 Native 函數中設置返回值 (空)
 */
void KReturnVoid();

/**
 * @brief 獲取參數數量 (用於手動綁定模式)
 * @return 參數個數
 */
int KGetArgCount();

/**
 * @brief 獲取整數類型參數
 * @param index 參數索引
 * @return 整數參數值
 */
KInt KGetArgInt(int index);

/**
 * @brief 獲取布爾類型參數
 * @param index 參數索引
 * @return 布爾參數值
 */
KBool KGetArgBool(int index);

/**
 * @brief 獲取浮點類型參數
 * @param index 參數索引
 * @return 浮點參數值
 */
KFloat KGetArgFloat(int index);

/**
 * @brief 獲取字符串類型參數
 * @param index 參數索引
 * @return 字符串參數值
 */
KString KGetArgString(int index);

#ifdef __cplusplus
}
#endif

#endif // KORELIN_KAPI_H
