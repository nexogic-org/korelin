#include "kcache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief 輔助函數：寫入字符串 */
static int write_string(FILE* fp, const char* str) {
    if (!str) {
        uint32_t len = 0;
        if (fwrite(&len, sizeof(len), 1, fp) != 1) return -1;
        return 0;
    }
    
    uint32_t len = (uint32_t)strlen(str);
    if (fwrite(&len, sizeof(len), 1, fp) != 1) return -1;
    
    if (len > 0) {
        if (fwrite(str, 1, len, fp) != len) return -1;
    }
    return 0;
}

/** @brief 輔助函數：讀取字符串 */
static char* read_string(FILE* fp) {
    uint32_t len;
    if (fread(&len, sizeof(len), 1, fp) != 1) return NULL;
    
    if (len == 0) {
        return strdup("");
    }
    
    char* str = (char*)malloc(len + 1);
    if (!str) return NULL;
    
    if (fread(str, 1, len, fp) != len) {
        free(str);
        return NULL;
    }
    str[len] = '\0';
    return str;
}

int kcache_save(const char* filename, KBytecodeChunk* chunk, uint64_t source_timestamp, uint64_t source_size) {
    if (!filename || !chunk) return -1;

    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    KCacheHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = KCACHE_MAGIC;
    header.version = KCACHE_VERSION;
    header.timestamp = source_timestamp;
    header.source_size = source_size;
    
    header.code_size = (uint32_t)chunk->count; /**< 字節碼實際字節數 */
    header.string_count = (uint32_t)chunk->string_count;
    header.lines_size = (uint32_t)(chunk->count * sizeof(int)); /**< lines 數組大小通常與 code count 一致 */

    // 1. 寫入頭部
    if (fwrite(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    // 2. 寫入字節碼指令
    if (header.code_size > 0) {
        if (fwrite(chunk->code, 1, header.code_size, fp) != header.code_size) {
            fclose(fp);
            return -1;
        }
    }

    // 3. 寫入行號信息
    if (header.lines_size > 0) {
        if (fwrite(chunk->lines, 1, header.lines_size, fp) != header.lines_size) {
            fclose(fp);
            return -1;
        }
    }

    // 4. 寫入字符串常量池
    for (size_t i = 0; i < chunk->string_count; i++) {
        if (write_string(fp, chunk->string_table[i]) != 0) {
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

int kcache_load(const char* filename, KBytecodeChunk* chunk, uint64_t source_timestamp, uint64_t source_size) {
    if (!filename || !chunk) return -1;

    FILE* fp = fopen(filename, "rb");
    if (!fp) return -1; // 文件不存在或無法讀取

    KCacheHeader header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    // 驗證魔數和版本
    if (header.magic != KCACHE_MAGIC || header.version != KCACHE_VERSION) {
        fclose(fp);
        return 1; /**< 格式無效或版本不匹配 */
    }

    // 驗證源文件一致性 (如果提供了 timestamp/size)
    if (source_timestamp != 0 && header.timestamp != source_timestamp) {
        fclose(fp);
        return 1; /**< 緩存過期 */
    }
    if (source_size != 0 && header.source_size != source_size) {
        fclose(fp);
        return 1; /**< 緩存過期 */
    }

    // 開始加載數據
    // 確保 chunk 已初始化（通常調用者已調用 init_chunk）
    // 我們需要根據 header 分配內存

    // 1. 加載字節碼
    if (header.code_size > 0) {
        chunk->capacity = header.code_size; /**< 精確分配 */
        chunk->count = header.code_size;
        chunk->code = (uint8_t*)malloc(chunk->capacity);
        if (!chunk->code) { fclose(fp); return -1; }
        
        if (fread(chunk->code, 1, header.code_size, fp) != header.code_size) {
            free(chunk->code);
            fclose(fp);
            return -1;
        }
    }

    // 2. 加載行號
    if (header.lines_size > 0) {
        // lines 數組的元素個數應該等於 code count
        size_t lines_count = header.lines_size / sizeof(int);
        if (lines_count != chunk->count) {
             // 簡單校驗：lines 大小應該匹配
        }
        
        chunk->lines = (int*)malloc(header.lines_size);
        if (!chunk->lines) {
            // 清理已分配的 code
            if (chunk->code) free(chunk->code);
            fclose(fp);
            return -1;
        }

        if (fread(chunk->lines, 1, header.lines_size, fp) != header.lines_size) {
            free(chunk->code);
            free(chunk->lines);
            fclose(fp);
            return -1;
        }
    }

    // 3. 加載字符串常量池
    if (header.string_count > 0) {
        chunk->string_count = header.string_count;
        chunk->string_table = (char**)malloc(header.string_count * sizeof(char*));
        if (!chunk->string_table) {
            if (chunk->code) free(chunk->code);
            if (chunk->lines) free(chunk->lines);
            fclose(fp);
            return -1;
        }

        for (uint32_t i = 0; i < header.string_count; i++) {
            char* str = read_string(fp);
            if (!str) {
                // 加載失敗，需要清理
                for (uint32_t j = 0; j < i; j++) free(chunk->string_table[j]);
                free(chunk->string_table);
                if (chunk->code) free(chunk->code);
                if (chunk->lines) free(chunk->lines);
                fclose(fp);
                return -1;
            }
            chunk->string_table[i] = str;
        }
    }

    fclose(fp);
    return 0; // 成功
}
