/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../include/wifi.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "link_protocol/smart_config.h"

#define NMCLI_SEP_CHAR ':'

static bool g_inited = false;
static struct wifi_state g_state;
static wifi_msg_cb_t g_cb = NULL;
static void *g_cb_arg = NULL;
static char *g_scan_ssid = NULL;
static char *g_ap_ssid = NULL;
static char *g_ap_psk = NULL;
static char *g_hotspot_name = NULL;
static struct wifi_ap_config g_ap_cache;
static volatile int g_linkd_done = 0;
static char g_linkd_ssid[WIFI_SSID_MAX_LEN + 1];
static char g_linkd_psk[WIFI_PSK_MAX_LEN + 1];
static struct wifi_linkd_result g_linkd_result;

static char *wifi_strdup(const char *src)
{
    size_t len;
    char *dst;

    if (!src)
        return NULL;

    len = strlen(src);
    dst = malloc(len + 1);
    if (!dst)
        return NULL;
    memcpy(dst, src, len);
    dst[len] = '\0';
    return dst;
}

static void wifi_emit_msg(struct wifi_msg_data *msg)
{
    if (!msg || !g_cb)
        return;
    msg->private_data = g_cb_arg;
    g_cb(msg);
}

#ifdef WIFI_DEBUG
static void debug_print_nmcli(char *const argv[])
{
    int i = 0;

    if (!argv || !argv[0])
        return;

    fprintf(stderr, "[WIFI] nmcli cmd:");
    while (argv[i]) {
        fprintf(stderr, " %s", argv[i]);
        i++;
    }
    fprintf(stderr, "\n");
}
#endif

static char *read_all(int fd)
{
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);

    if (!buf)
        return NULL;

    for (;;) {
        char tmp[256];
        ssize_t r = read(fd, tmp, sizeof(tmp));
        if (r <= 0)
            break;
        if (len + (size_t)r + 1 > cap) {
            size_t new_cap = (len + (size_t)r + 1) * 2;
            char *new_buf = realloc(buf, new_cap);
            if (!new_buf) {
                free(buf);
                return NULL;
            }
            buf = new_buf;
            cap = new_cap;
        }
        memcpy(buf + len, tmp, (size_t)r);
        len += (size_t)r;
    }

    buf[len] = '\0';
    return buf;
}

static void rstrip(char *s)
{
    size_t len;

    if (!s)
        return;
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

static int run_nmcli(char *const argv[], char **output)
{
    int pipefd[2];
    pid_t pid;
    int status = 0;
    char *out_buf = NULL;

#ifdef WIFI_DEBUG
    debug_print_nmcli(argv);
#endif

    if (pipe(pipefd) != 0)
        return -1;

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp("nmcli", argv);
        _exit(127);
    }

    close(pipefd[1]);
    out_buf = read_all(pipefd[0]);
    close(pipefd[0]);

    if (waitpid(pid, &status, 0) < 0)
        return -1;

    if (output) {
        if (!out_buf) {
            out_buf = malloc(1);
            if (out_buf)
                out_buf[0] = '\0';
        }
        *output = out_buf;
    } else {
        free(out_buf);
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

static int split_fields(char *line, char sep, char **fields, int max_fields)
{
    int count = 0;
    char *src = line;
    char *dst = line;

    if (!line || !fields || max_fields <= 0)
        return 0;

    fields[count++] = line;
    while (*src) {
        if (*src == '\\' && src[1] != '\0') {
            src++;
            *dst++ = *src++;
            continue;
        }
        if (*src == sep && count < max_fields) {
            *dst++ = '\0';
            fields[count++] = dst;
            src++;
            continue;
        }
        *dst++ = *src++;
    }
    *dst = '\0';
    return count;
}

static bool is_active_field(const char *field)
{
    if (!field)
        return false;
    return (strcmp(field, "*") == 0) ||
        (strcasecmp(field, "yes") == 0) ||
        (strcasecmp(field, "true") == 0);
}

static bool is_wifi_type(const char *type)
{
    if (!type)
        return false;
    return (strcasecmp(type, "wifi") == 0) ||
        (strcasecmp(type, "802-11-wireless") == 0);
}

static bool parse_mac(const char *text, uint8_t *mac)
{
    unsigned int tmp[6];
    if (!text || !mac)
        return false;
    if (sscanf(text, "%02x:%02x:%02x:%02x:%02x:%02x",
        &tmp[0], &tmp[1], &tmp[2], &tmp[3], &tmp[4], &tmp[5]) != 6)
        return false;
    for (int i = 0; i < 6; ++i)
        mac[i] = (uint8_t)tmp[i];
    return true;
}

static void format_mac(const uint8_t *mac, char *buf, size_t buf_sz)
{
    if (!mac || !buf || buf_sz < 18)
        return;
    snprintf(buf, buf_sz, "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static bool parse_ipv4(const char *text, uint8_t *addr)
{
    unsigned int a, b, c, d;
    char tmp[32];
    const char *slash;

    if (!text || !addr)
        return false;

    slash = strchr(text, '/');
    if (slash) {
        size_t len = (size_t)(slash - text);
        if (len >= sizeof(tmp))
            return false;
        memcpy(tmp, text, len);
        tmp[len] = '\0';
        text = tmp;
    }

    if (sscanf(text, "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
        return false;
    if (a > 255 || b > 255 || c > 255 || d > 255)
        return false;
    addr[0] = (uint8_t)a;
    addr[1] = (uint8_t)b;
    addr[2] = (uint8_t)c;
    addr[3] = (uint8_t)d;
    return true;
}

static bool is_zero_ipv4(const uint8_t *addr)
{
    if (!addr)
        return true;
    return addr[0] == 0 && addr[1] == 0 && addr[2] == 0 && addr[3] == 0;
}

static void format_ipv4(const uint8_t *addr, char *buf, size_t buf_sz)
{
    if (!addr || !buf || buf_sz < 16)
        return;
    snprintf(buf, buf_sz, "%u.%u.%u.%u",
        addr[0], addr[1], addr[2], addr[3]);
}

static void smart_config_linkd_cb(smart_config_result *result, SMART_CONFIG_STA state)
{
    if (state != SMART_CONFIG_COMPLETE || !result)
        return;

    strncpy(g_linkd_ssid, result->ssid, WIFI_SSID_MAX_LEN);
    g_linkd_ssid[WIFI_SSID_MAX_LEN] = '\0';
    strncpy(g_linkd_psk, result->psk, WIFI_PSK_MAX_LEN);
    g_linkd_psk[WIFI_PSK_MAX_LEN] = '\0';
    g_linkd_result.ssid = g_linkd_ssid;
    g_linkd_result.psk = g_linkd_psk;
    g_linkd_done = 1;
}

static bool contains_ci(const char *haystack, const char *needle)
{
    size_t nlen;

    if (!haystack || !needle || !*needle)
        return false;

    nlen = strlen(needle);
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (p[i] && i < nlen &&
            tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen)
            return true;
    }
    return false;
}

static enum wifi_secure parse_security(const char *sec)
{
    if (!sec || !*sec || strcmp(sec, "--") == 0)
        return WIFI_SEC_NONE;

    if (contains_ci(sec, "WPA3") || contains_ci(sec, "SAE"))
        return WIFI_SEC_WPA3_PSK;
    if (contains_ci(sec, "WPA2"))
        return WIFI_SEC_WPA2_PSK;
    if (contains_ci(sec, "WPA") || contains_ci(sec, "WPA1"))
        return WIFI_SEC_WPA_PSK;
    if (contains_ci(sec, "WEP"))
        return WIFI_SEC_WEP;
    if (contains_ci(sec, "EAP"))
        return WIFI_SEC_EAP;
    return WIFI_SEC_UNKNOWN;
}

static enum wifi_status nmcli_get_wifi_device(char *buf, size_t buf_sz)
{
    char *output = NULL;
    char *saveptr = NULL;
    char *line;
    char *fields[3];
    char *argv[] = {
        "nmcli", "-t",
        "-f", "DEVICE,TYPE,STATE", "device", "status", NULL
    };
    int rc = run_nmcli(argv, &output);
    if (rc != 0 || !output) {
        free(output);
        return WIFI_STATUS_FAIL;
    }

    line = strtok_r(output, "\n", &saveptr);
    while (line) {
        int count;
        rstrip(line);
        if (!*line) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        count = split_fields(line, NMCLI_SEP_CHAR, fields, 3);
        if (count >= 3 && is_wifi_type(fields[1])) {
            if (strcmp(fields[2], "unavailable") != 0) {
                strncpy(buf, fields[0], buf_sz - 1);
                buf[buf_sz - 1] = '\0';
                free(output);
                return WIFI_STATUS_SUCCESS;
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(output);
    return WIFI_STATUS_NOT_READY;
}

static enum wifi_status nmcli_get_active_connection(char *buf, size_t buf_sz)
{
    char *output = NULL;
    char *saveptr = NULL;
    char *line;
    char *fields[3];
    char *argv[] = {
        "nmcli", "-t",
        "-f", "NAME,TYPE,DEVICE", "connection", "show", "--active", NULL
    };
    int rc = run_nmcli(argv, &output);
    if (rc != 0 || !output) {
        free(output);
        return WIFI_STATUS_FAIL;
    }

    line = strtok_r(output, "\n", &saveptr);
    while (line) {
        rstrip(line);
        if (!*line) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        if (split_fields(line, NMCLI_SEP_CHAR, fields, 3) >= 2 &&
            is_wifi_type(fields[1])) {
            strncpy(buf, fields[0], buf_sz - 1);
            buf[buf_sz - 1] = '\0';
            free(output);
            return WIFI_STATUS_SUCCESS;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(output);
    return WIFI_STATUS_NOT_READY;
}

static enum wifi_status nmcli_get_active_connection_by_device(const char *dev,
    char *buf, size_t buf_sz)
{
    char *output = NULL;
    char *saveptr = NULL;
    char *line;
    char *fields[3];
    char *argv[] = {
        "nmcli", "-t",
        "-f", "NAME,TYPE,DEVICE", "connection", "show", "--active", NULL
    };
    int rc;

    if (!dev || !buf || buf_sz == 0)
        return WIFI_STATUS_INVALID;

    rc = run_nmcli(argv, &output);
    if (rc != 0 || !output) {
        free(output);
        return WIFI_STATUS_FAIL;
    }

    line = strtok_r(output, "\n", &saveptr);
    while (line) {
        rstrip(line);
        if (!*line) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        if (split_fields(line, NMCLI_SEP_CHAR, fields, 3) >= 3 &&
            is_wifi_type(fields[1]) && strcmp(fields[2], dev) == 0) {
            strncpy(buf, fields[0], buf_sz - 1);
            buf[buf_sz - 1] = '\0';
            free(output);
            return WIFI_STATUS_SUCCESS;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(output);
    return WIFI_STATUS_NOT_READY;
}

static enum wifi_status nmcli_get_active_ap_connection(char *conn, size_t conn_sz,
    char *dev, size_t dev_sz)
{
    char *output = NULL;
    char *saveptr = NULL;
    char *line;
    char *fields[3];
    char *argv[] = {
        "nmcli", "-t",
        "-f", "NAME,TYPE,DEVICE", "connection", "show", "--active", NULL
    };
    int rc;

    if (!conn || !dev || conn_sz == 0 || dev_sz == 0)
        return WIFI_STATUS_INVALID;

    rc = run_nmcli(argv, &output);
    if (rc != 0 || !output) {
        free(output);
        return WIFI_STATUS_FAIL;
    }

    line = strtok_r(output, "\n", &saveptr);
    while (line) {
        rstrip(line);
        if (!*line) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        if (split_fields(line, NMCLI_SEP_CHAR, fields, 3) >= 3 &&
            is_wifi_type(fields[1]) && fields[2] && *fields[2]) {
            char *mode_out = NULL;
            char *argv_mode[] = {
                "nmcli", "-t",
                "-f", "802-11-wireless.mode", "connection", "show", fields[0], NULL
            };
            rc = run_nmcli(argv_mode, &mode_out);
            if (rc == 0 && mode_out) {
                char *mode_save = NULL;
                char *mode_line = strtok_r(mode_out, "\n", &mode_save);
                while (mode_line) {
                    char *mode_fields[2];
                    rstrip(mode_line);
                    if (split_fields(mode_line, NMCLI_SEP_CHAR, mode_fields, 2) >= 2 &&
                        strcmp(mode_fields[0], "802-11-wireless.mode") == 0 &&
                        strcasecmp(mode_fields[1], "ap") == 0) {
                        strncpy(conn, fields[0], conn_sz - 1);
                        conn[conn_sz - 1] = '\0';
                        strncpy(dev, fields[2], dev_sz - 1);
                        dev[dev_sz - 1] = '\0';
                        free(mode_out);
                        free(output);
                        return WIFI_STATUS_SUCCESS;
                    }
                    mode_line = strtok_r(NULL, "\n", &mode_save);
                }
            }
            free(mode_out);
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(output);
    return WIFI_STATUS_NOT_READY;
}

static enum wifi_status nmcli_get_device_info(const char *dev, uint8_t *ip, uint8_t *gw)
{
    char *output = NULL;
    char *saveptr = NULL;
    char *line;
    char *fields[2];
    char *argv[] = {
        "nmcli", "-t",
        "-f", "IP4.ADDRESS,IP4.GATEWAY", "device", "show", (char *)dev, NULL
    };
    int rc;
    bool got_ip = false;
    bool got_gw = false;

    if (!dev)
        return WIFI_STATUS_INVALID;

    rc = run_nmcli(argv, &output);
    if (rc != 0 || !output) {
        free(output);
        return WIFI_STATUS_FAIL;
    }

    line = strtok_r(output, "\n", &saveptr);
    while (line) {
        int count;
        rstrip(line);
        if (!*line) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        count = split_fields(line, NMCLI_SEP_CHAR, fields, 2);
        if (count >= 2) {
            if (strcmp(fields[0], "IP4.ADDRESS[1]") == 0 ||
                strcmp(fields[0], "IP4.ADDRESS") == 0) {
                if (parse_ipv4(fields[1], ip))
                    got_ip = true;
            } else if (strcmp(fields[0], "IP4.GATEWAY") == 0) {
                if (parse_ipv4(fields[1], gw))
                    got_gw = true;
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(output);
    return (got_ip || got_gw) ? WIFI_STATUS_SUCCESS : WIFI_STATUS_NOT_READY;
}

static enum wifi_status nmcli_get_hwaddr(const char *dev, uint8_t *mac)
{
    char *output = NULL;
    char *saveptr = NULL;
    char *line;
    char *fields[2];
    char *argv[] = {
        "nmcli", "-t",
        "-f", "GENERAL.HWADDR", "device", "show", (char *)dev, NULL
    };
    int rc;

    if (!dev || !mac)
        return WIFI_STATUS_INVALID;

    rc = run_nmcli(argv, &output);
    if (rc != 0 || !output) {
        free(output);
        return WIFI_STATUS_FAIL;
    }

    line = strtok_r(output, "\n", &saveptr);
    while (line) {
        int count;
        rstrip(line);
        if (!*line) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        count = split_fields(line, NMCLI_SEP_CHAR, fields, 2);
        if (count >= 2 && strcmp(fields[0], "GENERAL.HWADDR") == 0) {
            if (parse_mac(fields[1], mac)) {
                free(output);
                return WIFI_STATUS_SUCCESS;
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(output);
    return WIFI_STATUS_FAIL;
}

static enum wifi_status nmcli_ap_disable_internal(void)
{
    char *output = NULL;
    int rc = -1;

    if (g_hotspot_name && *g_hotspot_name) {
        char *argv[] = { "nmcli", "connection", "down", g_hotspot_name, NULL };
        rc = run_nmcli(argv, &output);
        free(output);
        if (rc == 0)
            return WIFI_STATUS_SUCCESS;
    }

    {
        char wifi_dev[64];
        if (nmcli_get_wifi_device(wifi_dev, sizeof(wifi_dev)) != WIFI_STATUS_SUCCESS)
            return WIFI_STATUS_NOT_READY;
        char *argv[] = { "nmcli", "device", "disconnect", wifi_dev, NULL };
        output = NULL;
        rc = run_nmcli(argv, &output);
        free(output);
        return (rc == 0) ? WIFI_STATUS_SUCCESS : WIFI_STATUS_FAIL;
    }
}

static void clear_ap_cache(void)
{
    free(g_ap_ssid);
    free(g_ap_psk);
    free(g_hotspot_name);
    g_ap_ssid = NULL;
    g_ap_psk = NULL;
    g_hotspot_name = NULL;
    memset(&g_ap_cache, 0, sizeof(g_ap_cache));
}

static enum wifi_status refresh_ap_cache_from_nmcli(void)
{
    char conn[WIFI_SSID_MAX_LEN + 1];
    char dev[64];
    char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
    char psk[WIFI_PSK_MAX_LEN + 1] = {0};
    unsigned int channel = 0;
    enum wifi_secure sec = WIFI_SEC_UNKNOWN;
    char *output = NULL;
    char *saveptr = NULL;
    char *line;
    char *argv[] = {
        "nmcli", "-t",
        "-f", "802-11-wireless.ssid,802-11-wireless.channel,"
        "802-11-wireless-security.key-mgmt,802-11-wireless-security.psk",
        "connection", "show", conn, NULL
    };
    int rc;

    if (nmcli_get_active_ap_connection(conn, sizeof(conn), dev, sizeof(dev)) != WIFI_STATUS_SUCCESS)
        return WIFI_STATUS_NOT_READY;

    rc = run_nmcli(argv, &output);
    if (rc != 0 || !output) {
        free(output);
        return WIFI_STATUS_FAIL;
    }

    line = strtok_r(output, "\n", &saveptr);
    while (line) {
        char *fields[2];
        rstrip(line);
        if (!*line) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        if (split_fields(line, NMCLI_SEP_CHAR, fields, 2) >= 2) {
            if (strcmp(fields[0], "802-11-wireless.ssid") == 0) {
                strncpy(ssid, fields[1], WIFI_SSID_MAX_LEN);
                ssid[WIFI_SSID_MAX_LEN] = '\0';
            } else if (strcmp(fields[0], "802-11-wireless.channel") == 0) {
                channel = (unsigned int)atoi(fields[1]);
            } else if (strcmp(fields[0], "802-11-wireless-security.key-mgmt") == 0) {
                sec = parse_security(fields[1]);
            } else if (strcmp(fields[0], "802-11-wireless-security.psk") == 0) {
                strncpy(psk, fields[1], WIFI_PSK_MAX_LEN);
                psk[WIFI_PSK_MAX_LEN] = '\0';
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(output);

    if (!*ssid)
        return WIFI_STATUS_NOT_READY;

    if (sec == WIFI_SEC_UNKNOWN && !*psk)
        sec = WIFI_SEC_NONE;

    clear_ap_cache();
    g_ap_ssid = wifi_strdup(ssid);
    g_ap_psk = wifi_strdup(psk);
    g_hotspot_name = wifi_strdup(conn);
    memset(&g_ap_cache, 0, sizeof(g_ap_cache));
    g_ap_cache.ssid = g_ap_ssid;
    g_ap_cache.psk = g_ap_psk;
    g_ap_cache.sec = sec;
    g_ap_cache.channel = (channel > 255) ? 0 : (uint8_t)channel;

    if (nmcli_get_hwaddr(dev, g_ap_cache.mac_addr) != WIFI_STATUS_SUCCESS)
        memset(g_ap_cache.mac_addr, 0, sizeof(g_ap_cache.mac_addr));
    if (nmcli_get_device_info(dev, g_ap_cache.ip_addr, g_ap_cache.gw_addr) != WIFI_STATUS_SUCCESS) {
        memset(g_ap_cache.ip_addr, 0, sizeof(g_ap_cache.ip_addr));
        memset(g_ap_cache.gw_addr, 0, sizeof(g_ap_cache.gw_addr));
    }

    g_state.ap_state = WIFI_AP_STATE_ENABLE;
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_init(void)
{
    char *output = NULL;
    char *argv[] = { "nmcli", "-t", "-f", "RUNNING", "general", NULL };
    int rc;

    if (g_inited)
        return WIFI_STATUS_SUCCESS;

    rc = run_nmcli(argv, &output);
    free(output);
    if (rc != 0)
        return WIFI_STATUS_FAIL;

    g_inited = true;
    memset(&g_state, 0, sizeof(g_state));
    g_state.support_mode = WIFI_MODE_STATION_AP;
    g_state.current_mode = WIFI_MODE_UNKNOWN;
    g_state.current_mode_init_flag = 1;
    g_state.current_mode_enable_flag = 0;
    g_state.sta_state = WIFI_STA_IDLE;
    g_state.ap_state = WIFI_AP_STATE_DISABLE;
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_deinit(void)
{
    if (!g_inited)
        return WIFI_STATUS_SUCCESS;

    g_inited = false;
    memset(&g_state, 0, sizeof(g_state));
    free(g_scan_ssid);
    g_scan_ssid = NULL;
    clear_ap_cache();
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_on(enum wifi_mode mode)
{
    char *output = NULL;
    char *argv[] = { "nmcli", "radio", "wifi", "on", NULL };
    int rc;
    struct wifi_msg_data msg;

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;

    rc = run_nmcli(argv, &output);
    free(output);
    if (rc != 0)
        return WIFI_STATUS_FAIL;

    g_state.current_mode = mode;
    g_state.current_mode_enable_flag = 1;
    msg.id = WIFI_MSG_ID_DEV_STATUS;
    msg.data.d_status = WIFI_DEV_STATUS_UP;
    wifi_emit_msg(&msg);
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_off(enum wifi_mode mode)
{
    char *output = NULL;
    char *argv[] = { "nmcli", "radio", "wifi", "off", NULL };
    char conn[WIFI_SSID_MAX_LEN + 1];
    int rc;
    struct wifi_msg_data msg;

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;

    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_STATION_AP)
        (void)nmcli_ap_disable_internal();

    if (nmcli_get_active_connection(conn, sizeof(conn)) == WIFI_STATUS_SUCCESS) {
        char *argv_down[] = { "nmcli", "connection", "down", conn, NULL };
        (void)run_nmcli(argv_down, &output);
        free(output);
        output = NULL;
    }

    rc = run_nmcli(argv, &output);
    free(output);
    if (rc != 0)
        return WIFI_STATUS_FAIL;

    g_state.current_mode = WIFI_MODE_UNKNOWN;
    g_state.current_mode_enable_flag = 0;
    msg.id = WIFI_MSG_ID_DEV_STATUS;
    msg.data.d_status = WIFI_DEV_STATUS_DOWN;
    wifi_emit_msg(&msg);
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_sta_connect(struct wifi_sta_connect_param *param)
{
    char *output = NULL;
    char bssid_str[18];
    char *argv[10];
    int idx = 0;
    int rc;
    bool has_bssid = false;
    struct wifi_msg_data msg;

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;
    if (!param || !param->ssid)
        return WIFI_STATUS_INVALID;

    memset(bssid_str, 0, sizeof(bssid_str));
    for (int i = 0; i < 6; ++i) {
        if (param->bssid[i] != 0) {
            has_bssid = true;
            break;
        }
    }
    if (has_bssid)
        format_mac(param->bssid, bssid_str, sizeof(bssid_str));

    argv[idx++] = "nmcli";
    argv[idx++] = "device";
    argv[idx++] = "wifi";
    argv[idx++] = "connect";
    argv[idx++] = (char *)param->ssid;
    if (has_bssid) {
        argv[idx++] = "bssid";
        argv[idx++] = bssid_str;
    }
    if (param->password && *param->password) {
        argv[idx++] = "password";
        argv[idx++] = (char *)param->password;
    }
    argv[idx] = NULL;

    rc = run_nmcli(argv, &output);
    free(output);
    if (rc != 0)
        return WIFI_STATUS_FAIL;

    g_state.sta_state = WIFI_STA_NET_CONNECTED;
    msg.id = WIFI_MSG_ID_STA_STATE_CHANGE;
    msg.data.state = WIFI_STA_NET_CONNECTED;
    wifi_emit_msg(&msg);
    msg.id = WIFI_MSG_ID_STA_CN_EVENT;
    msg.data.event = WIFI_STA_EV_NETWORK_UP;
    wifi_emit_msg(&msg);
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_sta_disconnect(void)
{
    char conn[WIFI_SSID_MAX_LEN + 1];
    char *output = NULL;
    /* Return immediately once NetworkManager accepts the disconnect request. */
    char *argv[] = { "nmcli", "-w", "0", "connection", "down", conn, NULL };
    int rc;
    struct wifi_msg_data msg;

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;

    if (nmcli_get_active_connection(conn, sizeof(conn)) != WIFI_STATUS_SUCCESS)
        return WIFI_STATUS_NOT_READY;

    rc = run_nmcli(argv, &output);
    free(output);
    if (rc != 0)
        return WIFI_STATUS_FAIL;

    g_state.sta_state = WIFI_STA_DISCONNECTED;
    msg.id = WIFI_MSG_ID_STA_STATE_CHANGE;
    msg.data.state = WIFI_STA_DISCONNECTED;
    wifi_emit_msg(&msg);
    msg.id = WIFI_MSG_ID_STA_CN_EVENT;
    msg.data.event = WIFI_STA_EV_NETWORK_DOWN;
    wifi_emit_msg(&msg);
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_sta_auto_reconnect(bool enable)
{
    char *output = NULL;
    char *scan = NULL;
    char *saveptr = NULL;
    char *line;
    char *argv[] = {
        "nmcli", "-t",
        "-f", "NAME,TYPE", "connection", "show", NULL
    };
    int rc;
    int modified = 0;

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;

    rc = run_nmcli(argv, &output);
    if (rc != 0 || !output) {
        free(output);
        return WIFI_STATUS_FAIL;
    }

    scan = wifi_strdup(output);
    if (!scan) {
        free(output);
        return WIFI_STATUS_NOMEM;
    }

    line = strtok_r(scan, "\n", &saveptr);
    while (line) {
        char *fields_mod[2];
        if (split_fields(line, NMCLI_SEP_CHAR, fields_mod, 2) >= 2 &&
            is_wifi_type(fields_mod[1])) {
            char *argv_mod[] = {
                "nmcli", "connection", "modify",
                fields_mod[0],
                "connection.autoconnect",
                enable ? "yes" : "no",
                NULL
            };
            char *mod_out = NULL;
            if (run_nmcli(argv_mod, &mod_out) == 0)
                modified++;
            free(mod_out);
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(scan);
    free(output);
    return (modified > 0) ? WIFI_STATUS_SUCCESS : WIFI_STATUS_NOT_READY;
}

enum wifi_status wifi_sta_auto_connect(const char *ssid)
{
    char *output = NULL;
    char *argv_up[] = { "nmcli", "connection", "up", (char *)ssid, NULL };
    char *argv_connect[] = { "nmcli", "device", "wifi", "connect", (char *)ssid, NULL };
    int rc;

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;
    if (!ssid || !*ssid)
        return WIFI_STATUS_INVALID;

    rc = run_nmcli(argv_up, &output);
    free(output);
    if (rc == 0)
        return WIFI_STATUS_SUCCESS;

    output = NULL;
    rc = run_nmcli(argv_connect, &output);
    free(output);
    return (rc == 0) ? WIFI_STATUS_SUCCESS : WIFI_STATUS_FAIL;
}

enum wifi_status wifi_sta_get_info(struct wifi_sta_info *info)
{
    char *output = NULL;
    char *saveptr = NULL;
    char *line;
    char *fields[6];
    char wifi_dev[64];
    char active_name[WIFI_SSID_MAX_LEN + 1];
    char *argv[] = {
        "nmcli", "-t",
        "-f", "ACTIVE,BSSID,SSID,FREQ,SIGNAL,SECURITY", "device", "wifi", "list", NULL
    };
    int rc;
    bool found = false;
    bool have_active = false;

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;
    if (!info)
        return WIFI_STATUS_INVALID;

    memset(info, 0, sizeof(*info));

    if (nmcli_get_wifi_device(wifi_dev, sizeof(wifi_dev)) != WIFI_STATUS_SUCCESS)
        return WIFI_STATUS_NOT_READY;

    if (nmcli_get_active_connection(active_name, sizeof(active_name)) == WIFI_STATUS_SUCCESS)
        have_active = true;

    rc = run_nmcli(argv, &output);
    if (rc != 0 || !output) {
        free(output);
        return WIFI_STATUS_FAIL;
    }

    line = strtok_r(output, "\n", &saveptr);
    while (line) {
        rstrip(line);
        if (!*line) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        if (split_fields(line, NMCLI_SEP_CHAR, fields, 6) >= 6 &&
            (is_active_field(fields[0]) ||
            (have_active && strcmp(fields[2], active_name) == 0))) {
            strncpy(info->ssid, fields[2], WIFI_SSID_MAX_LEN);
            info->ssid[WIFI_SSID_MAX_LEN] = '\0';
            parse_mac(fields[1], info->bssid);
            info->freq = atoi(fields[3]);
            info->rssi = atoi(fields[4]);
            info->sec = parse_security(fields[5]);
            found = true;
            break;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(output);

    if (!found) {
        if (have_active) {
            strncpy(info->ssid, active_name, WIFI_SSID_MAX_LEN);
            info->ssid[WIFI_SSID_MAX_LEN] = '\0';
            info->sec = WIFI_SEC_UNKNOWN;
            info->freq = 0;
            info->rssi = 0;
        } else {
            return WIFI_STATUS_NOT_READY;
        }
    }

    if (nmcli_get_hwaddr(wifi_dev, info->mac_addr) != WIFI_STATUS_SUCCESS)
        memset(info->mac_addr, 0, sizeof(info->mac_addr));

    if (nmcli_get_device_info(wifi_dev, info->ip_addr, info->gw_addr) != WIFI_STATUS_SUCCESS) {
        memset(info->ip_addr, 0, sizeof(info->ip_addr));
        memset(info->gw_addr, 0, sizeof(info->gw_addr));
    }

    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_sta_list_networks(struct wifi_sta_list *list)
{
    char *output = NULL;
    char *scan = NULL;
    char *saveptr = NULL;
    char *line;
    char *argv[] = {
        "nmcli", "-t",
        "-f", "NAME,TYPE", "connection", "show", NULL
    };
    int rc;
    int total = 0;
    int sys_total = 0;
    int idx = 0;

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;
    if (!list)
        return WIFI_STATUS_INVALID;

    list->nodes = NULL;
    list->list_num = 0;
    list->sys_list_num = 0;

    rc = run_nmcli(argv, &output);
    if (rc != 0 || !output) {
        free(output);
        return WIFI_STATUS_FAIL;
    }

    scan = wifi_strdup(output);
    if (!scan) {
        free(output);
        return WIFI_STATUS_NOMEM;
    }

    line = strtok_r(scan, "\n", &saveptr);
    while (line) {
        char *fields[2];
        if (split_fields(line, NMCLI_SEP_CHAR, fields, 2) >= 2 &&
            is_wifi_type(fields[1]))
            total++;
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(scan);

    sys_total = total;
    if (total == 0) {
        free(output);
        return WIFI_STATUS_SUCCESS;
    }

    if (total > WIFI_STA_MAX_NUM)
        total = WIFI_STA_MAX_NUM;

    list->nodes = calloc((size_t)total, sizeof(struct wifi_sta_list_node));
    if (!list->nodes) {
        free(output);
        return WIFI_STATUS_NOMEM;
    }

    saveptr = NULL;
    line = strtok_r(output, "\n", &saveptr);
    while (line && idx < total) {
        char *fields[2];
        if (split_fields(line, NMCLI_SEP_CHAR, fields, 2) >= 2 &&
            is_wifi_type(fields[1])) {
            list->nodes[idx].id = idx;
            strncpy(list->nodes[idx].ssid, fields[0], WIFI_SSID_MAX_LEN);
            list->nodes[idx].ssid[WIFI_SSID_MAX_LEN] = '\0';
            strncpy(list->nodes[idx].flags, "wifi", sizeof(list->nodes[idx].flags) - 1);
            list->nodes[idx].flags[sizeof(list->nodes[idx].flags) - 1] = '\0';
            idx++;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    list->list_num = idx;
    list->sys_list_num = sys_total;
    free(output);
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_sta_remove_networks(const char *ssid)
{
    char *output = NULL;
    int rc;

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;

    if (ssid && *ssid) {
        char *argv[] = { "nmcli", "connection", "delete", (char *)ssid, NULL };
        rc = run_nmcli(argv, &output);
        free(output);
        return (rc == 0) ? WIFI_STATUS_SUCCESS : WIFI_STATUS_FAIL;
    }

    {
        char *list_out = NULL;
        char *scan = NULL;
        char *saveptr = NULL;
        char *line;
        char *argv_list[] = {
            "nmcli", "-t",
            "-f", "NAME,TYPE", "connection", "show", NULL
        };
        int removed = 0;

        rc = run_nmcli(argv_list, &list_out);
        if (rc != 0 || !list_out) {
            free(list_out);
            return WIFI_STATUS_FAIL;
        }

        scan = wifi_strdup(list_out);
        if (!scan) {
            free(list_out);
            return WIFI_STATUS_NOMEM;
        }

        line = strtok_r(scan, "\n", &saveptr);
        while (line) {
            char *fields[2];
            if (split_fields(line, NMCLI_SEP_CHAR, fields, 2) >= 2 &&
                is_wifi_type(fields[1])) {
                char *argv_del[] = { "nmcli", "connection", "delete", fields[0], NULL };
                char *del_out = NULL;
                if (run_nmcli(argv_del, &del_out) == 0)
                    removed++;
                free(del_out);
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }

        free(scan);
        free(list_out);
        return (removed > 0) ? WIFI_STATUS_SUCCESS : WIFI_STATUS_NOT_READY;
    }
}

enum wifi_status wifi_ap_enable(struct wifi_ap_config *config)
{
    char wifi_dev[64];
    char channel_str[8];
    char *argv[14];
    int idx = 0;
    char *output = NULL;
    int rc;
    struct wifi_msg_data msg;
    bool ip_set = false;
    bool gw_set = false;
    char ip_str[16];
    char gw_str[16];
    char ip_cidr[20];

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;
    if (!config || !config->ssid)
        return WIFI_STATUS_INVALID;

    if (nmcli_get_wifi_device(wifi_dev, sizeof(wifi_dev)) != WIFI_STATUS_SUCCESS)
        return WIFI_STATUS_NOT_READY;

    if (config->sec != WIFI_SEC_NONE && (!config->psk || !*config->psk))
        return WIFI_STATUS_INVALID;

    ip_set = !is_zero_ipv4(config->ip_addr);
    gw_set = !is_zero_ipv4(config->gw_addr);
    if (ip_set && !gw_set)
        memcpy(config->gw_addr, config->ip_addr, sizeof(config->gw_addr));

    argv[idx++] = "nmcli";
    argv[idx++] = "device";
    argv[idx++] = "wifi";
    argv[idx++] = "hotspot";
    argv[idx++] = "ifname";
    argv[idx++] = wifi_dev;
    argv[idx++] = "ssid";
    argv[idx++] = config->ssid;
    argv[idx++] = "con-name";
    argv[idx++] = config->ssid;

    if (config->channel > 0) {
        snprintf(channel_str, sizeof(channel_str), "%u", config->channel);
        argv[idx++] = "channel";
        argv[idx++] = channel_str;
    }

    if (config->psk && *config->psk) {
        argv[idx++] = "password";
        argv[idx++] = config->psk;
    }

    argv[idx] = NULL;

    rc = run_nmcli(argv, &output);
    free(output);
    if (rc != 0)
        return WIFI_STATUS_FAIL;

    if (ip_set) {
        char *mod_out = NULL;
        char *argv_mod[] = {
            "nmcli", "connection", "modify", config->ssid,
            "ipv4.method", "shared",
            "ipv4.addresses", ip_cidr,
            "ipv4.gateway", gw_str,
            NULL
        };
        char *argv_down[] = { "nmcli", "connection", "down", config->ssid, NULL };
        char *argv_up[] = { "nmcli", "connection", "up", config->ssid, NULL };

        format_ipv4(config->ip_addr, ip_str, sizeof(ip_str));
        format_ipv4(config->gw_addr, gw_str, sizeof(gw_str));
        snprintf(ip_cidr, sizeof(ip_cidr), "%s/24", ip_str);

        rc = run_nmcli(argv_mod, &mod_out);
        free(mod_out);
        if (rc != 0)
            return WIFI_STATUS_FAIL;

        output = NULL;
        (void)run_nmcli(argv_down, &output);
        free(output);

        output = NULL;
        rc = run_nmcli(argv_up, &output);
        free(output);
        if (rc != 0)
            return WIFI_STATUS_FAIL;
    }

    clear_ap_cache();
    g_ap_ssid = wifi_strdup(config->ssid);
    g_ap_psk = wifi_strdup(config->psk ? config->psk : "");
    g_hotspot_name = wifi_strdup(config->ssid);
    g_ap_cache = *config;
    g_ap_cache.dev_list = NULL;
    g_ap_cache.sta_dev_list_num = 0;
    g_ap_cache.sta_num = 0;
    g_ap_cache.ssid = g_ap_ssid;
    g_ap_cache.psk = g_ap_psk;

    g_state.ap_state = WIFI_AP_STATE_ENABLE;
    msg.id = WIFI_MSG_ID_AP_STATE_CHANGE;
    msg.data.ap_state = WIFI_AP_STATE_ENABLE;
    wifi_emit_msg(&msg);
    msg.id = WIFI_MSG_ID_AP_CN_EVENT;
    msg.data.ap_event = WIFI_AP_EV_ENABLED;
    wifi_emit_msg(&msg);
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_ap_disable(void)
{
    enum wifi_status ret;
    struct wifi_msg_data msg;

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;

    ret = nmcli_ap_disable_internal();
    if (ret != WIFI_STATUS_SUCCESS)
        return ret;

    clear_ap_cache();
    g_state.ap_state = WIFI_AP_STATE_DISABLE;
    msg.id = WIFI_MSG_ID_AP_STATE_CHANGE;
    msg.data.ap_state = WIFI_AP_STATE_DISABLE;
    wifi_emit_msg(&msg);
    msg.id = WIFI_MSG_ID_AP_CN_EVENT;
    msg.data.ap_event = WIFI_AP_EV_DISABLED;
    wifi_emit_msg(&msg);
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_ap_get_config(struct wifi_ap_config *config)
{
    if (!g_inited)
        return WIFI_STATUS_NOT_READY;
    if (!config)
        return WIFI_STATUS_INVALID;
    if (refresh_ap_cache_from_nmcli() == WIFI_STATUS_SUCCESS) {
        *config = g_ap_cache;
        return WIFI_STATUS_SUCCESS;
    }
    if (!g_ap_ssid)
        return WIFI_STATUS_NOT_READY;

    *config = g_ap_cache;
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_set_scan_param(struct wifi_scan_param *param)
{
    free(g_scan_ssid);
    g_scan_ssid = NULL;
    if (param && param->ssid) {
        g_scan_ssid = wifi_strdup(param->ssid);
        if (!g_scan_ssid)
            return WIFI_STATUS_NOMEM;
    }
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_get_scan_results(struct wifi_scan_result *result,
    const char *ssid, uint32_t *bss_num, uint32_t arr_size)
{
    char *output = NULL;
    char *saveptr = NULL;
    char *line;
    char *argv[] = {
        "nmcli", "-t",
        "-f", "BSSID,SSID,FREQ,SIGNAL,SECURITY", "device", "wifi", "list", NULL
    };
    int rc;
    uint32_t total = 0;
    uint32_t stored = 0;
    const char *filter_ssid = ssid;
    const bool use_default_filter = (ssid == NULL);

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;
    if (!bss_num)
        return WIFI_STATUS_INVALID;
    *bss_num = 0;
    if (arr_size > 0 && !result)
        return WIFI_STATUS_INVALID;

    if (use_default_filter && g_scan_ssid)
        filter_ssid = g_scan_ssid;
    else if (filter_ssid && filter_ssid[0] == '\0')
        filter_ssid = NULL;

    rc = run_nmcli(argv, &output);
    if (rc != 0 || !output) {
        free(output);
        return WIFI_STATUS_FAIL;
    }

    line = strtok_r(output, "\n", &saveptr);
    while (line) {
        rstrip(line);
        if (!*line) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        {
            char *fields[5];
            if (split_fields(line, NMCLI_SEP_CHAR, fields, 5) < 5) {
                line = strtok_r(NULL, "\n", &saveptr);
                continue;
            }
            if (!fields[1] || !*fields[1] || strcmp(fields[1], "--") == 0) {
                line = strtok_r(NULL, "\n", &saveptr);
                continue;
            }
            if (filter_ssid && strcmp(filter_ssid, fields[1]) != 0) {
                line = strtok_r(NULL, "\n", &saveptr);
                continue;
            }
            total++;
            if (stored < arr_size) {
                struct wifi_scan_result *entry = &result[stored];
                memset(entry, 0, sizeof(*entry));
                parse_mac(fields[0], entry->bssid);
                strncpy(entry->ssid, fields[1], WIFI_SSID_MAX_LEN);
                entry->ssid[WIFI_SSID_MAX_LEN] = '\0';
                entry->freq = (uint32_t)atoi(fields[2]);
                entry->rssi = atoi(fields[3]);
                entry->key_mgmt = parse_security(fields[4]);
                entry->scan_action = false;
                stored++;
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(output);
    *bss_num = total;
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_set_mac(const char *ifname, const uint8_t *mac_addr)
{
    char conn[WIFI_SSID_MAX_LEN + 1];
    char mac_str[18];
    char *output = NULL;
    char *argv[] = {
        "nmcli", "connection", "modify",
        conn,
        "802-11-wireless.cloned-mac-address",
        mac_str,
        NULL
    };
    char *argv_down[] = { "nmcli", "connection", "down", conn, NULL };
    char *argv_up[] = { "nmcli", "connection", "up", conn, NULL };
    int rc;

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;
    if (!ifname || !mac_addr)
        return WIFI_STATUS_INVALID;

    if (nmcli_get_active_connection_by_device(ifname, conn, sizeof(conn)) != WIFI_STATUS_SUCCESS)
        return WIFI_STATUS_NOT_READY;

    format_mac(mac_addr, mac_str, sizeof(mac_str));
    rc = run_nmcli(argv, &output);
    free(output);
    if (rc != 0)
        return WIFI_STATUS_FAIL;

    output = NULL;
    (void)run_nmcli(argv_down, &output);
    free(output);

    output = NULL;
    rc = run_nmcli(argv_up, &output);
    free(output);
    return (rc == 0) ? WIFI_STATUS_SUCCESS : WIFI_STATUS_FAIL;
}

enum wifi_status wifi_get_mac(const char *ifname, uint8_t *mac_addr)
{
    char wifi_dev[64];

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;
    if (!mac_addr)
        return WIFI_STATUS_INVALID;

    if (!ifname || !*ifname) {
        if (nmcli_get_wifi_device(wifi_dev, sizeof(wifi_dev)) != WIFI_STATUS_SUCCESS)
            return WIFI_STATUS_NOT_READY;
        ifname = wifi_dev;
    }

    return nmcli_get_hwaddr(ifname, mac_addr);
}

enum wifi_status wifi_linkd_protocol(enum wifi_linkd_mode mode, wifi_msg_cb_t cb, void *params,
    int second)
{
    int timeout_ms;
    uint16_t port = 0;

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;

    if (mode != WIFI_LINKD_MODE_SOFTAP)
        return WIFI_STATUS_UNSUPPORTED;

    if (g_state.ap_state == WIFI_AP_STATE_DISABLE)
        return WIFI_STATUS_NOT_READY;

    g_linkd_done = 0;
    g_linkd_result.ssid = NULL;
    g_linkd_result.psk = NULL;

    if (params) {
        port = *(uint16_t *)params;
        if (port > 0 && smart_config_set_port(port) != 0) {
            return WIFI_STATUS_INVALID;
        }
    }

    if (smart_config_set_cb(smart_config_linkd_cb) != 0) {
        return WIFI_STATUS_FAIL;
    }

    if (smart_config_start() != 0) {
        return WIFI_STATUS_FAIL;
    }

    timeout_ms = ((second > 0) ? second : 300) * 1000;
    while (!g_linkd_done && timeout_ms > 0) {
        usleep(200 * 1000);
        timeout_ms -= 200;
    }

    (void)smart_config_stop();

    if (!g_linkd_done)
        return WIFI_STATUS_TIMEOUT;

    if (cb) {
        struct wifi_msg_data msg;
        memset(&msg, 0, sizeof(msg));
        msg.id = WIFI_MSG_ID_MAX;
        msg.private_data = &g_linkd_result;
        cb(&msg);
    }
    return WIFI_STATUS_SUCCESS;
}

enum wifi_status wifi_get_state(struct wifi_state *state)
{
    struct wifi_state tmp;
    char *output = NULL;
    char *argv[] = { "nmcli", "-t", "-f", "WIFI", "general", NULL };
    int rc;
    char conn[WIFI_SSID_MAX_LEN + 1];

    if (!g_inited)
        return WIFI_STATUS_NOT_READY;
    if (!state)
        return WIFI_STATUS_INVALID;

    tmp = g_state;

    rc = run_nmcli(argv, &output);
    if (rc == 0 && output) {
        rstrip(output);
        if (strstr(output, "enabled"))
            tmp.current_mode_enable_flag = 1;
        else if (strstr(output, "disabled"))
            tmp.current_mode_enable_flag = 0;
    }
    free(output);

    if (nmcli_get_active_connection(conn, sizeof(conn)) == WIFI_STATUS_SUCCESS)
        tmp.sta_state = WIFI_STA_NET_CONNECTED;
    else
        tmp.sta_state = WIFI_STA_DISCONNECTED;

    g_state = tmp;
    *state = tmp;
    return WIFI_STATUS_SUCCESS;
}
