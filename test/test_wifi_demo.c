/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/wifi.h"

static void print_mac(const uint8_t *mac)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int parse_mac(const char *text, uint8_t *mac)
{
    unsigned int tmp[6];
    if (!text || !mac)
        return -1;
    if (sscanf(text, "%02x:%02x:%02x:%02x:%02x:%02x",
        &tmp[0], &tmp[1], &tmp[2], &tmp[3], &tmp[4], &tmp[5]) != 6)
        return -1;
    for (int i = 0; i < 6; ++i)
        mac[i] = (uint8_t)tmp[i];
    return 0;
}

static int parse_ipv4(const char *text, uint8_t *addr)
{
    unsigned int tmp[4];
    if (!text || !addr)
        return -1;
    if (sscanf(text, "%u.%u.%u.%u", &tmp[0], &tmp[1], &tmp[2], &tmp[3]) != 4)
        return -1;
    for (int i = 0; i < 4; ++i) {
        if (tmp[i] > 255)
            return -1;
        addr[i] = (uint8_t)tmp[i];
    }
    return 0;
}

static void set_default_linkd_ap_config(struct wifi_ap_config *config)
{
    static char ssid[] = "TEST_AP_LINK";
    static char psk[] = "12345678";

    if (!config)
        return;

    memset(config, 0, sizeof(*config));
    config->ssid = ssid;
    config->psk = psk;
    config->sec = WIFI_SEC_WPA2_PSK;
    config->ip_addr[0] = 192;
    config->ip_addr[1] = 168;
    config->ip_addr[2] = 1;
    config->ip_addr[3] = 1;
    config->gw_addr[0] = 192;
    config->gw_addr[1] = 168;
    config->gw_addr[2] = 1;
    config->gw_addr[3] = 1;
}

static int build_ap_config(int argc, char **argv, int cmd_index, struct wifi_ap_config *config)
{
    const char *password = NULL;
    const char *arg1 = NULL;
    const char *arg2 = NULL;
    const char *arg3 = NULL;
    bool ip_set = false;

    if (!config)
        return -1;
    if (argc < cmd_index + 2 || argc > cmd_index + 5)
        return -1;

    memset(config, 0, sizeof(*config));
    config->ssid = argv[cmd_index + 1];
    arg1 = (argc > cmd_index + 2) ? argv[cmd_index + 2] : NULL;
    arg2 = (argc > cmd_index + 3) ? argv[cmd_index + 3] : NULL;
    arg3 = (argc > cmd_index + 4) ? argv[cmd_index + 4] : NULL;
    if (arg1) {
        if (parse_ipv4(arg1, config->ip_addr) == 0) {
            ip_set = true;
        } else {
            password = arg1;
        }
    }
    if (arg2) {
        if (!ip_set) {
            if (parse_ipv4(arg2, config->ip_addr) != 0) {
                printf("invalid ip: %s\n", arg2);
                return -2;
            }
            ip_set = true;
        } else {
            if (parse_ipv4(arg2, config->gw_addr) != 0) {
                printf("invalid gw: %s\n", arg2);
                return -2;
            }
        }
    }
    if (arg3) {
        if (parse_ipv4(arg3, config->gw_addr) != 0) {
            printf("invalid gw: %s\n", arg3);
            return -2;
        }
    }
    if (password) {
        config->psk = (char *)password;
        config->sec = WIFI_SEC_WPA2_PSK;
    } else {
        config->sec = WIFI_SEC_NONE;
    }
    return 0;
}

static enum wifi_mode parse_mode(const char *text)
{
    if (!text)
        return WIFI_MODE_UNKNOWN;
    if (strcmp(text, "station") == 0 || strcmp(text, "sta") == 0 || strcmp(text, "1") == 0)
        return WIFI_MODE_STATION;
    if (strcmp(text, "ap") == 0 || strcmp(text, "2") == 0)
        return WIFI_MODE_AP;
    if (strcmp(text, "station_ap") == 0 || strcmp(text, "sta_ap") == 0 || strcmp(text, "3") == 0)
        return WIFI_MODE_STATION_AP;
    return WIFI_MODE_UNKNOWN;
}

static void wifi_event_cb(struct wifi_msg_data *msg)
{
    if (!msg)
        return;

    switch (msg->id) {
    case WIFI_MSG_ID_DEV_STATUS:
        printf("[cb] dev_status=%d\n", msg->data.d_status);
        break;
    case WIFI_MSG_ID_STA_CN_EVENT:
        printf("[cb] sta_event=%d\n", msg->data.event);
        break;
    case WIFI_MSG_ID_STA_STATE_CHANGE:
        printf("[cb] sta_state=%d\n", msg->data.state);
        break;
    case WIFI_MSG_ID_AP_CN_EVENT:
        printf("[cb] ap_event=%d\n", msg->data.ap_event);
        break;
    case WIFI_MSG_ID_AP_STATE_CHANGE:
        printf("[cb] ap_state=%d\n", msg->data.ap_state);
        break;
    default:
        printf("[cb] msg_id=%d\n", msg->id);
        break;
    }
}

static void linkd_connect_cb(struct wifi_msg_data *msg)
{
    struct wifi_linkd_result *result;
    struct wifi_sta_connect_param param;
    enum wifi_status ret;

    if (!msg)
        return;

    result = (struct wifi_linkd_result *)msg->private_data;
    if (!result || !result->ssid)
        return;

    printf("[linkd] ssid=%s psk=%s\n",
        result->ssid ? result->ssid : "(null)",
        result->psk ? result->psk : "(null)");

    memset(&param, 0, sizeof(param));
    param.ssid = result->ssid;
    if (result->psk && *result->psk)
        param.password = result->psk;

    ret = wifi_sta_remove_networks("TEST_AP_LINK");
    printf("wifi_sta_remove_networks: %d\n", ret);

    ret = wifi_on(WIFI_MODE_STATION);
    if (ret != WIFI_STATUS_SUCCESS)
        printf("wifi_on failed: %d\n", ret);
    ret = wifi_sta_connect(&param);
    printf("wifi_sta_connect: %d\n", ret);
}

static void usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s cb <command> [args...]\n", prog);
    printf("  %s scan\n", prog);
    printf("  %s connect <ssid> [password]\n", prog);
    printf("  %s disconnect\n", prog);
    printf("  %s on <mode>\n", prog);
    printf("  %s off <mode>\n", prog);
    printf("  %s info\n", prog);
    printf("  %s list\n", prog);
    printf("  %s remove [ssid]\n", prog);
    printf("  %s auto_reconnect <0|1>\n", prog);
    printf("  %s auto_connect <ssid>\n", prog);
    printf("  %s ap <ssid> [password] [ip] [gw]\n", prog);
    printf("  %s linkd\n", prog);
    printf("  %s ap_off\n", prog);
    printf("  %s ap_get\n", prog);
    printf("  %s scan_param [ssid|clear]\n", prog);
    printf("  %s mac [ifname]\n", prog);
    printf("  %s setmac <ifname> <mac>\n", prog);
    printf("  %s state\n", prog);
}

int main(int argc, char **argv)
{
    enum wifi_status ret;
    int cmd_index = 1;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "cb") == 0) {
        cmd_index = 2;
        if (argc <= cmd_index) {
            usage(argv[0]);
            return 1;
        }
    }

    ret = wifi_init();
    if (ret != WIFI_STATUS_SUCCESS) {
        printf("wifi_init failed: %d\n", ret);
        return 1;
    }

    if (strcmp(argv[cmd_index], "scan") == 0) {
        struct wifi_scan_result results[32];
        uint32_t total = 0;
        ret = wifi_on(WIFI_MODE_STATION);
        if (ret != WIFI_STATUS_SUCCESS)
            printf("wifi_on failed: %d\n", ret);
        ret = wifi_get_scan_results(results, NULL, &total, 32);
        if (ret != WIFI_STATUS_SUCCESS) {
            printf("wifi_get_scan_results failed: %d\n", ret);
        } else {
            printf("Found %u networks (showing up to 32):\n", total);
            for (uint32_t i = 0; i < total && i < 32; ++i) {
                printf("  SSID=%s RSSI=%d FREQ=%u SEC=%d BSSID=",
                    results[i].ssid, results[i].rssi,
                    results[i].freq, results[i].key_mgmt);
                print_mac(results[i].bssid);
                printf("\n");
            }
        }
    } else if (strcmp(argv[cmd_index], "connect") == 0) {
        struct wifi_sta_connect_param param;
        if (argc < cmd_index + 2) {
            usage(argv[0]);
            wifi_deinit();
            return 1;
        }
        memset(&param, 0, sizeof(param));
        param.ssid = argv[cmd_index + 1];
        if (argc > cmd_index + 2)
            param.password = argv[cmd_index + 2];
        ret = wifi_on(WIFI_MODE_STATION);
        if (ret != WIFI_STATUS_SUCCESS)
            printf("wifi_on failed: %d\n", ret);
        ret = wifi_sta_connect(&param);
        printf("wifi_sta_connect: %d\n", ret);
    } else if (strcmp(argv[cmd_index], "disconnect") == 0) {
        ret = wifi_sta_disconnect();
        printf("wifi_sta_disconnect: %d\n", ret);
    } else if (strcmp(argv[cmd_index], "on") == 0) {
        enum wifi_mode mode;
        if (argc < cmd_index + 2) {
            usage(argv[0]);
            wifi_deinit();
            return 1;
        }
        mode = parse_mode(argv[cmd_index + 1]);
        ret = wifi_on(mode);
        printf("wifi_on: %d\n", ret);
    } else if (strcmp(argv[cmd_index], "off") == 0) {
        enum wifi_mode mode;
        if (argc < cmd_index + 2) {
            usage(argv[0]);
            wifi_deinit();
            return 1;
        }
        mode = parse_mode(argv[cmd_index + 1]);
        ret = wifi_off(mode);
        printf("wifi_off: %d\n", ret);
    } else if (strcmp(argv[cmd_index], "info") == 0) {
        struct wifi_sta_info info;
        ret = wifi_sta_get_info(&info);
        if (ret != WIFI_STATUS_SUCCESS) {
            printf("wifi_sta_get_info failed: %d\n", ret);
        } else {
            printf("SSID: %s\n", info.ssid);
            printf("BSSID: "); print_mac(info.bssid); printf("\n");
            printf("MAC: "); print_mac(info.mac_addr); printf("\n");
            printf("FREQ: %d\n", info.freq);
            printf("RSSI: %d\n", info.rssi);
            printf("SEC: %d\n", info.sec);
            printf("IP: %u.%u.%u.%u\n",
                info.ip_addr[0], info.ip_addr[1], info.ip_addr[2], info.ip_addr[3]);
            printf("GW: %u.%u.%u.%u\n",
                info.gw_addr[0], info.gw_addr[1], info.gw_addr[2], info.gw_addr[3]);
        }
    } else if (strcmp(argv[cmd_index], "list") == 0) {
        struct wifi_sta_list list;
        ret = wifi_sta_list_networks(&list);
        if (ret != WIFI_STATUS_SUCCESS) {
            printf("wifi_sta_list_networks failed: %d\n", ret);
        } else {
            printf("Saved networks: %d\n", list.list_num);
            for (int i = 0; i < list.list_num; ++i) {
                printf("  [%d] %s\n", list.nodes[i].id, list.nodes[i].ssid);
            }
        }
        free(list.nodes);
    } else if (strcmp(argv[cmd_index], "remove") == 0) {
        const char *ssid = (argc > cmd_index + 1) ? argv[cmd_index + 1] : NULL;
        ret = wifi_sta_remove_networks(ssid);
        printf("wifi_sta_remove_networks: %d\n", ret);
    } else if (strcmp(argv[cmd_index], "auto_reconnect") == 0) {
        if (argc < cmd_index + 2) {
            usage(argv[0]);
            wifi_deinit();
            return 1;
        }
        ret = wifi_sta_auto_reconnect(atoi(argv[cmd_index + 1]) != 0);
        printf("wifi_sta_auto_reconnect: %d\n", ret);
    } else if (strcmp(argv[cmd_index], "auto_connect") == 0) {
        if (argc < cmd_index + 2) {
            usage(argv[0]);
            wifi_deinit();
            return 1;
        }
        ret = wifi_sta_auto_connect(argv[cmd_index + 1]);
        printf("wifi_sta_auto_connect: %d\n", ret);
    } else if (strcmp(argv[cmd_index], "ap") == 0) {
        struct wifi_ap_config config;
        int ap_rc = build_ap_config(argc, argv, cmd_index, &config);
        if (ap_rc < 0) {
            if (ap_rc == -1)
                usage(argv[0]);
            wifi_deinit();
            return 1;
        }
        ret = wifi_on(WIFI_MODE_AP);
        if (ret != WIFI_STATUS_SUCCESS)
            printf("wifi_on failed: %d\n", ret);
        ret = wifi_ap_enable(&config);
        printf("wifi_ap_enable: %d\n", ret);
    } else if (strcmp(argv[cmd_index], "linkd") == 0) {
        struct wifi_ap_config config;
        if (argc != cmd_index + 1) {
            usage(argv[0]);
            wifi_deinit();
            return 1;
        }
        set_default_linkd_ap_config(&config);
        ret = wifi_on(WIFI_MODE_AP);
        if (ret != WIFI_STATUS_SUCCESS)
            printf("wifi_on failed: %d\n", ret);
        ret = wifi_ap_enable(&config);
        printf("wifi_ap_enable: %d\n", ret);
        if (ret == WIFI_STATUS_SUCCESS) {
            printf("linkd: connect to AP and open http://192.168.1.1:8000\n");
            ret = wifi_linkd_protocol(WIFI_LINKD_MODE_SOFTAP, linkd_connect_cb, NULL, 300);
            printf("wifi_linkd_protocol: %d\n", ret);
        }
    } else if (strcmp(argv[cmd_index], "ap_off") == 0) {
        ret = wifi_ap_disable();
        printf("wifi_ap_disable: %d\n", ret);
    } else if (strcmp(argv[cmd_index], "ap_get") == 0) {
        struct wifi_ap_config config;
        memset(&config, 0, sizeof(config));
        ret = wifi_ap_get_config(&config);
        if (ret != WIFI_STATUS_SUCCESS) {
            printf("wifi_ap_get_config failed: %d\n", ret);
        } else {
            printf("AP SSID: %s\n", config.ssid ? config.ssid : "(null)");
            printf("AP PSK: %s\n", config.psk ? config.psk : "(null)");
            printf("AP SEC: %d\n", config.sec);
            printf("AP CH: %u\n", config.channel);
            printf("AP STA NUM: %u\n", config.sta_num);
        }
    } else if (strcmp(argv[cmd_index], "scan_param") == 0) {
        if (argc > cmd_index + 1 && strcmp(argv[cmd_index + 1], "clear") != 0) {
            struct wifi_scan_param param;
            param.ssid = argv[cmd_index + 1];
            ret = wifi_set_scan_param(&param);
        } else {
            ret = wifi_set_scan_param(NULL);
        }
        printf("wifi_set_scan_param: %d\n", ret);
    } else if (strcmp(argv[cmd_index], "mac") == 0) {
        uint8_t mac[6] = {0};
        const char *ifname = (argc > cmd_index + 1) ? argv[cmd_index + 1] : NULL;
        ret = wifi_get_mac(ifname, mac);
        if (ret == WIFI_STATUS_SUCCESS) {
            printf("MAC: ");
            print_mac(mac);
            printf("\n");
        } else {
            printf("wifi_get_mac failed: %d\n", ret);
        }
    } else if (strcmp(argv[cmd_index], "setmac") == 0) {
        uint8_t mac[6] = {0};
        if (argc < cmd_index + 3) {
            usage(argv[0]);
            wifi_deinit();
            return 1;
        }
        if (parse_mac(argv[cmd_index + 2], mac) != 0) {
            printf("invalid mac: %s\n", argv[cmd_index + 2]);
            wifi_deinit();
            return 1;
        }
        ret = wifi_set_mac(argv[cmd_index + 1], mac);
        printf("wifi_set_mac: %d\n", ret);
    } else if (strcmp(argv[cmd_index], "state") == 0) {
        struct wifi_state state;
        ret = wifi_get_state(&state);
        if (ret != WIFI_STATUS_SUCCESS) {
            printf("wifi_get_state failed: %d\n", ret);
        } else {
            printf("support_mode=%u current_mode=%u init=%u enable=%u sta_state=%d ap_state=%d\n",
                state.support_mode, state.current_mode,
                state.current_mode_init_flag, state.current_mode_enable_flag,
                state.sta_state, state.ap_state);
        }
    } else {
        usage(argv[0]);
    }

    wifi_deinit();
    return 0;
}
