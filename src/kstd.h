#ifndef KORELIN_KSTD_H
#define KORELIN_KSTD_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化標準庫
 * 註冊所有內置模塊 (os, math, time 等)
 */
void kstd_register();

#ifdef __cplusplus
}
#endif

#endif // KORELIN_KSTD_H
