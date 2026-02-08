#ifndef KORELIN_KAPI_H
#define KORELIN_KAPI_H

#include "kvm.h" // Needed for KVM*

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** @brief 基本類型定義 */

typedef int64_t KInt;
typedef bool KBool;
typedef double KFloat;
typedef const char* KString;

/** @brief 核心 API */

/** @brief 初始化 Korelin 環境 (可選，KRun 會自動調用) */
void KInit();

/** @brief 綁定現有 VM 到 API */
void KBindVM(KVM* vm);

/** @brief 銷毀 Korelin 環境 */
void KCleanup();

/** @brief 運行 Korelin 代碼字符串 */
void KRun(const char* source);

/** @brief 庫管理 */

/** @brief 註冊一個新包 (庫) */
void KLibNew(const char* package_name);

/** @brief 向包中添加成員 */
/**< type: "function", "var", "const", "subpackage", "class" */
/**< value: 函數指針或變量指針 */
void KLibAdd(const char* package_name, const char* type, const char* name, void* value);

/** @brief 類管理 */

/** @brief 創建一個新的全局類 */
void KLibNewClass(const char* class_name);

/** @brief 向類中添加方法 (Native Function) */
void KLibAddMethod(const char* class_name, const char* method_name, void* func);

/** @brief 向全局作用域添加函數 */
void KLibAddGlobal(const char* name, void* func);

typedef enum {
    KMOD_PUBLIC = 0,
    KMOD_PRIVATE = 1,
    KMOD_PROTECTED = 2,
    KMOD_STATIC = 4
} KModifier;

/** @brief 向類中添加成員 (方法或屬性) */
/**< member_type: "method", "field" */
/**< modifier: KModifier 標誌位 (e.g. KMOD_PUBLIC | KMOD_STATIC) */
void KLibClassAdd(const char* class_name, const char* member_type, const char* name, void* value, int modifier);

/** @brief 函數實現輔助 */

/** @brief 在 Native 函數中設置返回值 */
void KReturnInt(KInt val);
void KReturnBool(KBool val);
void KReturnFloat(KFloat val);
void KReturnString(KString val);
void KReturnVoid();

/** @brief 獲取參數數量 (用於手動綁定模式) */
int KGetArgCount();
KInt KGetArgInt(int index);
KBool KGetArgBool(int index);
KFloat KGetArgFloat(int index);
KString KGetArgString(int index);

#ifdef __cplusplus
}
#endif

#endif // KORELIN_KAPI_H
