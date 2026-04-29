/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SMART_CONFIG_H
#define SMART_CONFIG_H

#ifdef __cplusplus
#include <cstdio>
#else
#include <stdio.h>
#endif

#include <stdint.h>

#define SAC_DBG_ON     0
#define SAC_WRN_ON     1
#define SAC_ERR_ON     1

#define SAC_LOG(flags, fmt, arg...) \
    do {                            \
        if (flags)                  \
            printf(fmt, ##arg);     \
    } while (0)

#define SAC_DBG(fmt, arg...)                           \
    do {                                               \
        SAC_LOG(SAC_DBG_ON, "[SAC DBG] <%s():%d> " fmt, \
                __func__, __LINE__, ##arg);            \
    } while (0)

#define SAC_WRN(fmt, arg...)                           \
    do {                                               \
        SAC_LOG(SAC_WRN_ON, "[SAC WRN] <%s():%d> " fmt, \
                __func__, __LINE__, ##arg);            \
    } while (0)

#define SAC_ERR(fmt, arg...)                           \
    do {                                               \
        SAC_LOG(SAC_ERR_ON, "[SAC ERR] <%s():%d> " fmt, \
                __func__, __LINE__, ##arg);            \
    } while (0)

#define BASE64_BUF_SIZE(s) (((s) + 2) / 3 * 4 + 1)

typedef struct {
    char ssid[32];
    char psk[32];
} smart_config_result;

typedef enum {
    SMART_CONFIG_STOP,
    SMART_CONFIG_START,
    SMART_CONFIG_COMPLETE,
} SMART_CONFIG_STA;

typedef void (*smart_config_cb)(smart_config_result *result,
                                SMART_CONFIG_STA state);

int smart_config_start(void);
int smart_config_stop(void);
int smart_config_set_port(uint16_t port);
int smart_config_set_cb(smart_config_cb cb);
int smart_config_get_result(smart_config_result *result);
int smart_config_get_state(void);

#endif /* SMART_CONFIG_H */
