/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wifi.h"
#include "link_protocol/smart_config.h"

static int g_failures;

int smart_config_start(void)
{
    return 0;
}

int smart_config_stop(void)
{
    return 0;
}

int smart_config_set_port(uint16_t port)
{
    return (port == 0 || port >= 1024) ? 0 : -1;
}

int smart_config_set_cb(smart_config_cb cb)
{
    (void)cb;
    return 0;
}

int smart_config_get_result(smart_config_result *result)
{
    if (!result)
        return -1;
    strcpy(result->ssid, "HomeNet");
    strcpy(result->psk, "secret123");
    return 0;
}

int smart_config_get_state(void)
{
    return SMART_CONFIG_STOP;
}

#define CHECK_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL:%s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while (0)

#define CHECK_INT_EQ(actual, expected) do { \
    int _actual = (int)(actual); \
    int _expected = (int)(expected); \
    if (_actual != _expected) { \
        printf("FAIL:%s:%d: expected %s == %d, got %d\n", \
            __FILE__, __LINE__, #actual, _expected, _actual); \
        g_failures++; \
    } \
} while (0)

#define CHECK_STR_EQ(actual, expected) do { \
    const char *_actual = (actual); \
    const char *_expected = (expected); \
    if (strcmp(_actual, _expected) != 0) { \
        printf("FAIL:%s:%d: expected %s == '%s', got '%s'\n", \
            __FILE__, __LINE__, #actual, _expected, _actual); \
        g_failures++; \
    } \
} while (0)

static void reset_test_state(void)
{
    g_failures = 0;
}

static void test_error_paths(void)
{
    struct wifi_state state;
    struct wifi_sta_connect_param connect_param;
    struct wifi_scan_result scan_result;
    struct wifi_ap_config ap_config;
    uint32_t bss_num = 0;
    uint8_t mac[6];

    memset(&connect_param, 0, sizeof(connect_param));
    memset(&ap_config, 0, sizeof(ap_config));

    CHECK_INT_EQ(wifi_on(WIFI_MODE_STATION), WIFI_STATUS_NOT_READY);
    CHECK_INT_EQ(wifi_off(WIFI_MODE_STATION), WIFI_STATUS_NOT_READY);
    CHECK_INT_EQ(wifi_sta_disconnect(), WIFI_STATUS_NOT_READY);
    CHECK_INT_EQ(wifi_get_state(&state), WIFI_STATUS_NOT_READY);

    CHECK_INT_EQ(wifi_init(), WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(wifi_sta_connect(NULL), WIFI_STATUS_INVALID);
    CHECK_INT_EQ(wifi_sta_connect(&connect_param), WIFI_STATUS_INVALID);
    CHECK_INT_EQ(wifi_sta_auto_connect(NULL), WIFI_STATUS_INVALID);
    CHECK_INT_EQ(wifi_sta_auto_connect(""), WIFI_STATUS_INVALID);
    CHECK_INT_EQ(wifi_sta_get_info(NULL), WIFI_STATUS_INVALID);
    CHECK_INT_EQ(wifi_sta_list_networks(NULL), WIFI_STATUS_INVALID);
    CHECK_INT_EQ(wifi_ap_enable(NULL), WIFI_STATUS_INVALID);
    CHECK_INT_EQ(wifi_ap_get_config(NULL), WIFI_STATUS_INVALID);
    CHECK_INT_EQ(wifi_get_scan_results(NULL, NULL, NULL, 0), WIFI_STATUS_INVALID);
    CHECK_INT_EQ(wifi_get_scan_results(NULL, NULL, &bss_num, 1), WIFI_STATUS_INVALID);
    CHECK_INT_EQ(wifi_set_mac(NULL, mac), WIFI_STATUS_INVALID);
    CHECK_INT_EQ(wifi_set_mac("wlan0", NULL), WIFI_STATUS_INVALID);
    CHECK_INT_EQ(wifi_get_mac("wlan0", NULL), WIFI_STATUS_INVALID);

    ap_config.ssid = "RobotAP";
    ap_config.sec = WIFI_SEC_WPA2_PSK;
    ap_config.psk = NULL;
    CHECK_INT_EQ(wifi_ap_enable(&ap_config), WIFI_STATUS_INVALID);

    CHECK_INT_EQ(wifi_get_scan_results(&scan_result, "NoSuchSSID", &bss_num, 1),
        WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(bss_num, 0);
    CHECK_INT_EQ(wifi_deinit(), WIFI_STATUS_SUCCESS);
}

static void test_functional(void)
{
    struct wifi_state state;
    struct wifi_scan_param scan_param = {
        .ssid = "HomeNet",
    };
    struct wifi_scan_result results[2];
    uint32_t bss_num = 0;
    struct wifi_sta_info sta_info;
    struct wifi_sta_list list;
    struct wifi_sta_connect_param connect_param;
    struct wifi_ap_config ap_config;
    struct wifi_ap_config ap_out;
    uint8_t mac[6];
    uint8_t new_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

    CHECK_INT_EQ(wifi_init(), WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(wifi_on(WIFI_MODE_STATION), WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(wifi_get_state(&state), WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(state.current_mode_enable_flag, 1);

    CHECK_INT_EQ(wifi_set_scan_param(&scan_param), WIFI_STATUS_SUCCESS);
    memset(results, 0, sizeof(results));
    CHECK_INT_EQ(wifi_get_scan_results(results, NULL, &bss_num, 2), WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(bss_num, 1);
    CHECK_STR_EQ(results[0].ssid, "HomeNet");
    CHECK_INT_EQ(results[0].freq, 2412);
    CHECK_INT_EQ(results[0].rssi, 77);
    CHECK_INT_EQ(results[0].key_mgmt, WIFI_SEC_WPA2_PSK);
    CHECK_INT_EQ(results[0].bssid[0], 0xaa);

    memset(&sta_info, 0, sizeof(sta_info));
    CHECK_INT_EQ(wifi_sta_get_info(&sta_info), WIFI_STATUS_SUCCESS);
    CHECK_STR_EQ(sta_info.ssid, "HomeNet");
    CHECK_INT_EQ(sta_info.rssi, 77);
    CHECK_INT_EQ(sta_info.mac_addr[0], 0x12);
    CHECK_INT_EQ(sta_info.ip_addr[0], 192);

    memset(&list, 0, sizeof(list));
    CHECK_INT_EQ(wifi_sta_list_networks(&list), WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(list.list_num, 2);
    CHECK_STR_EQ(list.nodes[0].ssid, "HomeNet");
    free(list.nodes);

    memset(&connect_param, 0, sizeof(connect_param));
    connect_param.ssid = "HomeNet";
    connect_param.password = "secret123";
    CHECK_INT_EQ(wifi_sta_connect(&connect_param), WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(wifi_sta_disconnect(), WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(wifi_sta_auto_reconnect(true), WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(wifi_sta_auto_connect("HomeNet"), WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(wifi_sta_remove_networks("OldNet"), WIFI_STATUS_SUCCESS);

    memset(&ap_config, 0, sizeof(ap_config));
    ap_config.ssid = "RobotAP";
    ap_config.psk = "robotpass";
    ap_config.sec = WIFI_SEC_WPA2_PSK;
    ap_config.channel = 6;
    ap_config.ip_addr[0] = 192;
    ap_config.ip_addr[1] = 168;
    ap_config.ip_addr[2] = 50;
    ap_config.ip_addr[3] = 1;
    CHECK_INT_EQ(wifi_ap_enable(&ap_config), WIFI_STATUS_SUCCESS);
    memset(&ap_out, 0, sizeof(ap_out));
    CHECK_INT_EQ(wifi_ap_get_config(&ap_out), WIFI_STATUS_SUCCESS);
    CHECK_STR_EQ(ap_out.ssid, "RobotAP");
    CHECK_INT_EQ(ap_out.channel, 6);
    CHECK_INT_EQ(wifi_ap_disable(), WIFI_STATUS_SUCCESS);

    memset(mac, 0, sizeof(mac));
    CHECK_INT_EQ(wifi_get_mac("wlan0", mac), WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(mac[0], 0x12);
    CHECK_INT_EQ(wifi_set_mac("wlan0", new_mac), WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(wifi_off(WIFI_MODE_STATION), WIFI_STATUS_SUCCESS);
    CHECK_INT_EQ(wifi_deinit(), WIFI_STATUS_SUCCESS);
}

static int finish_test(const char *name)
{
    if (g_failures != 0) {
        printf("%s FAILED: %d failure(s)\n", name, g_failures);
        return 1;
    }
    printf("%s PASSED\n", name);
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "all";

    if (strcmp(mode, "functional") == 0) {
        reset_test_state();
        test_functional();
        return finish_test("wifi api functional test");
    }
    if (strcmp(mode, "error-paths") == 0) {
        reset_test_state();
        test_error_paths();
        return finish_test("wifi api error paths test");
    }
    if (strcmp(mode, "all") == 0) {
        reset_test_state();
        test_functional();
        if (finish_test("wifi api functional test") != 0)
            return 1;
        reset_test_state();
        test_error_paths();
        if (finish_test("wifi api error paths test") != 0)
            return 1;
        printf("wifi api contract test PASSED\n");
        return 0;
    }

    fprintf(stderr, "usage: %s [all|functional|error-paths]\n", argv[0]);
    return 2;
}
