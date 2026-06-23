#ifndef __MCUBOOT_LOGGING_H__
#define __MCUBOOT_LOGGING_H__

#include <stdio.h>

#define MCUBOOT_LOG_LEVEL           MCUBOOT_LOG_LEVEL_INFO
#define MCUBOOT_LOG_LEVEL_OFF       0
#define MCUBOOT_LOG_LEVEL_ERROR     1
#define MCUBOOT_LOG_LEVEL_WARNING   2
#define MCUBOOT_LOG_LEVEL_INFO      3
#define MCUBOOT_LOG_LEVEL_DEBUG     4

#define MCUBOOT_LOG_MODULE_DECLARE(domain)  /* no-op */
#define MCUBOOT_LOG_MODULE_REGISTER(domain) /* no-op */

#define MCUBOOT_LOG_ERR(...)    do { printf("[MCB E] " __VA_ARGS__); printf("\r\n"); } while (0)
#define MCUBOOT_LOG_WRN(...)    do { printf("[MCB W] " __VA_ARGS__); printf("\r\n"); } while (0)
#define MCUBOOT_LOG_INF(...)    do { printf("[MCB I] " __VA_ARGS__); printf("\r\n"); } while (0)
#define MCUBOOT_LOG_DBG(...)    do { } while (0)
#define MCUBOOT_LOG_SIM(...)    do { } while (0)

#endif /* __MCUBOOT_LOGGING_H__ */
