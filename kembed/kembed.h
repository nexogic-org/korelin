#ifndef KORELION_H
#define KORELION_H

#include "kapi.h"

/** @brief Korelin 單頭文件入口點 */
/**< 該頭文件連同此目錄中的源文件一起提供完整的 Korelin 引擎 API。 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化引擎 */
void korelion_init();

/** @brief 執行腳本字符串 */
void korelion_run(const char* source);

/** @brief 執行腳本文件 */
void korelion_run_file(const char* path);

/** @brief 清理 */
void korelion_cleanup();

#ifdef __cplusplus
}
#endif

#endif /** KORELION_H */
