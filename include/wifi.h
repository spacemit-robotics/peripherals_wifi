/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* constants */
#define WIFI_SSID_MAX_LEN    32
#define WIFI_PSK_MAX_LEN    64
#define WIFI_STA_MAX_NUM    20
#define WIFI_MODULE_NAME_MAX_LEN    64

/* status code */
enum wifi_status {
    WIFI_STATUS_SUCCESS = 0,
    WIFI_STATUS_FAIL = -1,
    WIFI_STATUS_NOT_READY = -2,
    WIFI_STATUS_NOMEM = -3,
    WIFI_STATUS_BUSY = -4,
    WIFI_STATUS_UNSUPPORTED = -5,
    WIFI_STATUS_INVALID = -6,
    WIFI_STATUS_TIMEOUT = -7,
    WIFI_STATUS_UNHANDLED = -8,
};

/* wifi mode */
enum wifi_mode {
    WIFI_MODE_UNKNOWN = 0,
    WIFI_MODE_STATION = 1,
    WIFI_MODE_AP = 2,
    WIFI_MODE_STATION_AP = 3,
};

/* device status */
enum wifi_dev_status {
    WIFI_DEV_STATUS_DOWN = 0,
    WIFI_DEV_STATUS_UP,
};

/* message ID */
enum wifi_msg_id {
    WIFI_MSG_ID_DEV_STATUS,
    WIFI_MSG_ID_STA_CN_EVENT,
    WIFI_MSG_ID_STA_STATE_CHANGE,
    WIFI_MSG_ID_AP_CN_EVENT,
    WIFI_MSG_ID_AP_STATE_CHANGE,
    WIFI_MSG_ID_MAX,
};

/* security type */
enum wifi_secure {
    WIFI_SEC_NONE = 0,
    WIFI_SEC_WEP = 1,
    WIFI_SEC_WPA_PSK = 2,
    WIFI_SEC_WPA2_PSK = 4,
    WIFI_SEC_WPA2_PSK_SHA256 = 8,
    WIFI_SEC_WPA3_PSK = 16,
    WIFI_SEC_EAP = 32,
    WIFI_SEC_UNKNOWN = 32768,
};

/* station state */
enum wifi_sta_state {
    WIFI_STA_IDLE,
    WIFI_STA_CONNECTING,
    WIFI_STA_CONNECTED,
    WIFI_STA_OBTAINING_IP,
    WIFI_STA_NET_CONNECTED,
    WIFI_STA_DHCP_TIMEOUT,
    WIFI_STA_DISCONNECTING,
    WIFI_STA_DISCONNECTED,
    WIFI_STA_BUSY_TIMEOUT,
};

/* station event */
enum wifi_sta_event {
    WIFI_STA_EV_DISCONNECTED,
    WIFI_STA_EV_SCAN_STARTED,
    WIFI_STA_EV_SCAN_SUCCESS,
    WIFI_STA_EV_SCAN_FAILED,
    WIFI_STA_EV_SCAN_RESULTS,
    WIFI_STA_EV_NETWORK_NOT_FOUND,
    WIFI_STA_EV_PASSWORD_INCORRECT,
    WIFI_STA_EV_CONNECT_TIMEOUT,
    WIFI_STA_EV_NETWORK_UP,
    WIFI_STA_EV_NETWORK_DOWN,
    WIFI_STA_EV_UNKNOWN,
};

/* station info */
struct wifi_sta_info {
    int id;
    int freq;
    int rssi;
    uint8_t bssid[6];
    char ssid[WIFI_SSID_MAX_LEN + 1];
    uint8_t mac_addr[6];
    uint8_t ip_addr[4];
    uint8_t gw_addr[4];
    enum wifi_secure sec;
};

/* station list node */
struct wifi_sta_list_node {
    int id;
    char ssid[WIFI_SSID_MAX_LEN + 1];
    uint8_t bssid[6];
    char flags[16];
};

/* station list */
struct wifi_sta_list {
    struct wifi_sta_list_node *nodes;
    int list_num;
    int sys_list_num;
};

/* station connect parameters */
struct wifi_sta_connect_param {
    const char *ssid;
    const char *password;
    uint8_t bssid[6];
    enum wifi_secure sec;
    bool fast_connect;
};

/* scan parameters */
struct wifi_scan_param {
    const char *ssid;
};

/* scan result */
struct wifi_scan_result {
    uint8_t bssid[6];
    char ssid[WIFI_SSID_MAX_LEN + 1];
    uint32_t freq;
    int rssi;
    enum wifi_secure key_mgmt;
    bool scan_action;
};

/* AP state */
enum wifi_ap_state {
    WIFI_AP_STATE_DISABLE,
    WIFI_AP_STATE_ENABLE,
    WIFI_AP_STATE_STA_CONNECTED,
};

/* AP event */
enum wifi_ap_event {
    WIFI_AP_EV_ENABLED = 1,
    WIFI_AP_EV_DISABLED,
    WIFI_AP_EV_STA_DISCONNECTED,
    WIFI_AP_EV_STA_CONNECTED,
    WIFI_AP_EV_UNKNOWN,
};

/* AP station member */
struct wifi_ap_sta_member {
    uint8_t sta_mac[6];
};

/* AP config */
struct wifi_ap_config {
    char *ssid;
    char *psk;
    enum wifi_secure sec;
    uint8_t channel;
    int key_mgmt;
    uint8_t max_num_sta;
    uint8_t mac_addr[6];
    uint8_t ip_addr[4];
    uint8_t gw_addr[4];
    struct wifi_ap_sta_member *dev_list;
    uint8_t sta_dev_list_num;
    uint8_t sta_num;
};

/* message data */
struct wifi_msg_data {
    enum wifi_msg_id id;
    union {
        enum wifi_dev_status d_status;
        enum wifi_sta_event event;
        enum wifi_sta_state state;
        enum wifi_ap_event ap_event;
        enum wifi_ap_state ap_state;
    } data;
    void *private_data;
};

/* linkd mode */
enum wifi_linkd_mode {
    WIFI_LINKD_MODE_BLE,
    WIFI_LINKD_MODE_SOFTAP,
    WIFI_LINKD_MODE_QRCODE,
    WIFI_LINKD_MODE_NONE,
};

/* linkd result */
struct wifi_linkd_result {
    char *ssid;
    char *psk;
};

/* message callback */
typedef void (*wifi_msg_cb_t)(struct wifi_msg_data *msg);

/* wifi state */
struct wifi_state {
    uint8_t support_mode;
    uint8_t current_mode;
    uint8_t current_mode_init_flag;
    uint8_t current_mode_enable_flag;
    enum wifi_sta_state sta_state;
    enum wifi_ap_state ap_state;
    void *os_private_data;
};

/* --- core API --- */

/* initialize wifi manager */
enum wifi_status wifi_init(void);

/* deinitialize wifi manager */
enum wifi_status wifi_deinit(void);

/* enable wifi with specified mode */
enum wifi_status wifi_on(enum wifi_mode mode);

/* disable wifi with specified mode */
enum wifi_status wifi_off(enum wifi_mode mode);

/* --- station API --- */

/* connect to specified AP */
enum wifi_status wifi_sta_connect(struct wifi_sta_connect_param *param);

/* disconnect from AP */
enum wifi_status wifi_sta_disconnect(void);

/* set auto reconnect */
enum wifi_status wifi_sta_auto_reconnect(bool enable);

/* auto connect to saved network */
enum wifi_status wifi_sta_auto_connect(const char *ssid);

/* get station information */
enum wifi_status wifi_sta_get_info(struct wifi_sta_info *info);

/* list saved networks */
enum wifi_status wifi_sta_list_networks(struct wifi_sta_list *list);

/* remove saved network(s) */
enum wifi_status wifi_sta_remove_networks(const char *ssid);

/* --- AP API --- */

/* enable AP with config */
enum wifi_status wifi_ap_enable(struct wifi_ap_config *config);

/* disable AP */
enum wifi_status wifi_ap_disable(void);

/* get AP config */
enum wifi_status wifi_ap_get_config(struct wifi_ap_config *config);

/* --- common API --- */

/* register message callback */
enum wifi_status wifi_register_msg_cb(wifi_msg_cb_t msg_cb, void *arg);

/* set scan parameters */
enum wifi_status wifi_set_scan_param(struct wifi_scan_param *param);

/* get scan results */
enum wifi_status wifi_get_scan_results(struct wifi_scan_result *result,
    const char *ssid, uint32_t *bss_num,
    uint32_t arr_size);

/* set MAC address */
enum wifi_status wifi_set_mac(const char *ifname, const uint8_t *mac_addr);

/* get MAC address */
enum wifi_status wifi_get_mac(const char *ifname, uint8_t *mac_addr);

/* linkd protocol (SOFTAP params: optional uint16_t* port) */
enum wifi_status wifi_linkd_protocol(enum wifi_linkd_mode mode, wifi_msg_cb_t cb, void *params,
    int second);

/* get wifi state */
enum wifi_status wifi_get_state(struct wifi_state *state);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_H */
