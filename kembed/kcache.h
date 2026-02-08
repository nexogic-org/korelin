//
//

#ifndef KORELIN_KCACHE_H
#define KORELIN_KCACHE_H

#include <stdint.h>
#include <stdbool.h>
#include "kcode.h"

// 緩存文件魔數 "KORE"
/** @brief 緩存文件魔數 "KORE" */
#define KCACHE_MAGIC 0x45524F4B
// 緩存版本號
#define KCACHE_VERSION 1

// 緩存文件頭部結構
/** @brief 緩存文件頭部結構 */
typedef struct {
    uint32_t magic;         // 文件標識
    uint32_t version;       // 版本號
    uint64_t timestamp;     // 源文件最後修改時間戳
    uint64_t source_size;   // 源文件大小（用於簡單校驗）
    
    uint32_t code_size;     // 字節碼大小 (bytes)
    uint32_t string_count;  // 字符串常量數量
    uint32_t lines_size;    // 行號表大小 (bytes)
    
    uint32_t reserved[4];   // 保留字段
} KCacheHeader;

/**
 * 將字節碼塊保存到緩存文件
 * @param filename 輸出文件名 (通常是 source.k.kc)
 * @param chunk 編譯好的字節碼塊
 * @param source_timestamp 源文件時間戳
 * @param source_size 源文件大小
 * @return 0 成功, 非0 失敗
 */
int kcache_save(const char* filename, KBytecodeChunk* chunk, uint64_t source_timestamp, uint64_t source_size);

/**
 * 從緩存文件加載字節碼
 * @param filename 緩存文件名
 * @param chunk 用於存儲加載數據的塊（需要預先 init_chunk）
 * @param source_timestamp 源文件當前時間戳（用於校驗）
 * @param source_size 源文件當前大小（用於校驗）
 * @return 0 成功, 1 緩存無效/過期, -1 讀取錯誤
 */
int kcache_load(const char* filename, KBytecodeChunk* chunk, uint64_t source_timestamp, uint64_t source_size);

#endif //KORELIN_KCACHE_H
