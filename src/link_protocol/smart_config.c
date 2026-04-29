/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <errno.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include <openssl/buffer.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

#include "smart_config.h"
#include "http_tp.h"

/* HTTP Constants */
#define APP_INFO             "Version: 0.1"
#define HTTP_DATA_MAX_LEN    2048

/* HTTP Return Codes
 * -1: failed and exit
 *  0: success and keep
 *  1: close and accept new
 *  2: success and exit
 */
#define HTTP_SUCCESS          (0)
#define HTTP_CLOSE            (1)
#define HTTP_EXIT             (2)
#define HTTP_ERROR            (-1)

/* Thread Configuration */
#define SMART_CONFIG_THREAD_STACK_SIZE    (2 * 1024)
#define SMART_CONFIG_THREAD_PRIO          (10)

/* Task States */
enum {
    TASK_INIT = 0,
    TASK_CREATED,
    TASK_RUNNING,
    TASK_EXIT,
};

/* HTTP Token Structure */
typedef struct {
    char *pToken1;    /* HTTP request method */
    char *pToken2;    /* HTTP path */
    char *pToken3;    /* URL function */
} http_token;


static char userName[] = "admin";
static char usrPassword[] = "admin";
static char *auth_str = NULL;
static char *http_buffer = NULL;
static int smart_config_server_fd = -1;
static int smart_config_client_fd = -1;
static SMART_CONFIG_STA smart_config_run_state = SMART_CONFIG_STOP;
static smart_config_cb smart_config_callback;

static pthread_t smart_config_thread;
static uint8_t smart_config_thread_state = TASK_INIT;

static smart_config_result smart_config_data;
static uint16_t smart_config_port = 8000;  // Default port

static int base64_encode(unsigned char *input, size_t length, char *output)
{
    BIO *bio = NULL;
    BIO *b64 = NULL;
    BUF_MEM *buf_mem = NULL;

    // create Base64 filter BIO and set no_newline mode
    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    // new ram for BIO to save result
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    // write data and encoding
    if (BIO_write(bio, input, length) <= 0) {
        SAC_ERR("%s BIO write error\n", __func__);
        goto cleanup;
    }
    BIO_flush(bio);

    // get result from BIO
    BIO_get_mem_ptr(bio, &buf_mem);

    // copy encoded_data to output buf
    memcpy(output, buf_mem->data, buf_mem->length);
    output[buf_mem->length] = '\0';

cleanup:
    BIO_free_all(bio);
    return (bio != NULL) ? 0 : -1;
}

static int auth_init(char *name, char *passwd)
{
    int ret = 0;
    uint32_t len;
    char *src_str = NULL;

    len = strlen(name) + strlen(passwd) + 2;

    if (auth_str)
        free(auth_str);

    int size = BASE64_BUF_SIZE(len);
    auth_str = malloc(size);
    if (auth_str == NULL) {
        ret = -1;
        goto out;
    }
    memset(auth_str, 0, size);

    src_str = malloc(len);
    if (src_str == 0) {
        ret = -1;
        goto out;
    }

    snprintf(src_str, len, "%s:%s", name, passwd);
    ret = base64_encode((unsigned char *)src_str, strlen(src_str), auth_str);
    if (ret != 0) {
        SAC_ERR("base64 encode err! ret:%d\n", ret);
        goto out;
    }

    SAC_DBG("auth_str:%s\n", auth_str);

out:
    if (src_str)
        free(src_str);

    return ret;
}

static int smart_config_http_init(void)
{
    int ret = 0;

    smart_config_client_fd = -1;
    smart_config_server_fd = -1;

    http_buffer = malloc(HTTP_DATA_MAX_LEN);
    if (http_buffer == NULL) {
        SAC_ERR("malloc http_buffer failed\n");
        return -1;
    }

    ret = auth_init(userName, usrPassword);
    if (ret != 0) {
        free(http_buffer);
        http_buffer = NULL;
        return -1;
    }

    return 0;
}

static int smart_config_http_deinit(void)
{
    if (auth_str) {
        free(auth_str);
        auth_str = NULL;
    }

    if (http_buffer) {
        free(http_buffer);
        http_buffer = NULL;
    }

    return 0;
}

static int send_http_data(int fd, char *data, int len)
{
    int w_size = 0, pos = 0;

    while (w_size < len) {
        pos = send(fd, (uint8_t *)data + w_size, len - w_size, 0);
        if (pos < 0) {
            SAC_ERR("send data err: %d\n", errno);
            return pos;
        }
        if (pos == 0) {
            SAC_ERR("Connection closed by peer\n");
            return -1;
        }
        w_size += pos;
        SAC_DBG("Sent %d bytes, total: %d/%d\n", pos, w_size, len);
    }

    return w_size;
}

static int http_parse(char *pStr, http_token *httpToken)
{
    char *pch = strchr(pStr, ' ');
    char *pch2 = strchr(pStr, '/');
    char *pch3;
    httpToken->pToken1 = pStr;

    if (pch) {
        *pch = '\0';
        pch++;
        pch3 = strchr(pch, ' ');
        if (pch2 && pch2 < pch3) {
            httpToken->pToken2 = pch2;
            pch2++;
            pch2 = strchr(pch2, '/');
            if (pch2 && pch2 < pch3) {
                pch2++;
                httpToken->pToken3 = pch2;
            } else {
                httpToken->pToken3 = NULL;
            }
        } else {
            httpToken->pToken2 = NULL;
        }
        return 1;
    }
    return 0;
}

static void html_decode(char *p, int len)
{
    int i, j, val;
    char ascii[4];

    for (i = 0; i < len; i++) {
        if (p[i] == '+') {
            p[i] = ' ';
        } else if (p[i] == '%') {
            if ((i + 2) >= len) {
                return;
            }

            ascii[0] = p[i + 1];
            ascii[1] = p[i + 2];
            ascii[2] = '\0';
            val = strtol(ascii, NULL, 16);

            p[i] = (char)val;
            for (j = i + 1; j < len - 2; j++) {
                p[j] = p[j + 2];
            }
            len -= 2;
        }
    }
}

static uint8_t http_post_parse(char **ppStr, const char *pFlag, char **ppValue)
{
    char *pch = strstr(*ppStr, pFlag);
    char *pch2 = NULL;

    if (pch) {
        pch2 = strchr(pch, '=');
        if (!pch2)
            return 0;
        pch2++;
        *ppValue = pch2;
        if (!(*ppValue))
            return 0;
        pch = strchr(pch2, '&');
        if (pch) {
            *pch = '\0';
            html_decode(pch2, strlen(pch2));
            *ppStr = pch + 1;
            return 1;
        }
    }

    return 0;
}

static void save_response(uint8_t result, int fd)
{
    if (result == 1) {
        snprintf(http_buffer, HTTP_DATA_MAX_LEN, HTTPSaveResponse,
            strlen(SaveResponseSucc), SaveResponseSucc);
    } else {
        snprintf(http_buffer, HTTP_DATA_MAX_LEN, HTTPSaveResponse,
            strlen(SaveResponseError), SaveResponseError);
    }

    send_http_data(fd, http_buffer, strlen(http_buffer));
}

static void send_system_page(int fd)
{
    char *body;
    uint32_t len = 0;

    memset(http_buffer, 0, HTTP_DATA_MAX_LEN);
    body = http_buffer;
    len = snprintf(body, HTTP_DATA_MAX_LEN, systemPage, APP_INFO,
        smart_config_data.ssid, smart_config_data.psk);

    len = snprintf(http_buffer, HTTP_DATA_MAX_LEN, headerPage, len);
    body = http_buffer + len;
    snprintf(body, HTTP_DATA_MAX_LEN - len, systemPage, APP_INFO,
        smart_config_data.ssid, smart_config_data.psk);

    send_http_data(fd, http_buffer, strlen(http_buffer));
}

static void get_settings_post(int fd, char *postdata, int len)
{
    char *pToken1, *pValue;
    pToken1 = postdata;
    (void)len;

    if (!(http_post_parse(&pToken1, "SSID", &pValue))) {
        SAC_DBG("save_response ssid\n");
        save_response(0, fd);
        return;
    }

    /* Validate and copy SSID */
    strncpy(smart_config_data.ssid, pValue, sizeof(smart_config_data.ssid) - 1);
    smart_config_data.ssid[sizeof(smart_config_data.ssid) - 1] = '\0';
    SAC_DBG("get ssid : %s\n", smart_config_data.ssid);

    if (!http_post_parse(&pToken1, "PSK", &pValue)) {
        SAC_DBG("save_response PSK\n");
        save_response(0, fd);
        return;
    }

    /* Validate and copy PSK */
    strncpy(smart_config_data.psk, pValue, sizeof(smart_config_data.psk) - 1);
    smart_config_data.psk[sizeof(smart_config_data.psk) - 1] = '\0';
    SAC_DBG("get psk : %s\n", smart_config_data.psk);

    if (strstr(pToken1, "save")) {
        SAC_DBG("save_response save\n");
        if (smart_config_callback) {
            smart_config_callback(&smart_config_data, SMART_CONFIG_COMPLETE);
        }
        smart_config_run_state = SMART_CONFIG_COMPLETE;
        save_response(1, fd);
        return;
    }
}

static int http_recv_request(int fd)
{
    int length = 0;
    int ret = 0;
    int content_length = 0;
    int pos = 0;
    char *str = NULL;

    SAC_DBG("recv request\n");
    memset(http_buffer, 0, HTTP_DATA_MAX_LEN);

    /* Receive HTTP header until \r\n\r\n */
    while (1) {
        ret = recv(fd, http_buffer + length, 1, 0);
        if (ret <= 0) {
            if (ret < 0)
                SAC_ERR("recv err! ret:%d errno:%d\n", ret, errno);
            return -1;
        }
        length += ret;
        /* Check for HTTP header terminator "\r\n\r\n" */
        if (length >= 4)
            if (memcmp(http_buffer + length - 4, "\r\n\r\n", 4) == 0)
                break;

        if (length >= HTTP_DATA_MAX_LEN)
            return -1;
    }

    /* Parse Content-Length header */
    str = strstr(http_buffer, "Content-Length:");
    if (str) {
        sscanf(str, "%*s %d", &content_length);
    }

    SAC_DBG("content length: %d\n", content_length);

    if (content_length >= HTTP_DATA_MAX_LEN - length)
        return -1;

    pos = length;
    length = 0;

    /* Receive HTTP body if content-length > 0 */
    if (content_length > 0) {
        while (1) {
            ret = recv(fd, http_buffer + pos + length, content_length - length, 0);
            if (ret <= 0) {
                if (ret < 0)
                    SAC_ERR("recv err! ret:%d errno:%d\n", ret, errno);
                return -1;
            }
            length += ret;
            if (length >= content_length)
                break;
        }
    }
    http_buffer[pos + length] = '\0';

    SAC_DBG("\n%s\n", http_buffer);

    return 0;
}

static int http_client_handle(int fd, int data_len)
{
    http_token httpToken = {0, 0, 0};
    int ret = HTTP_SUCCESS;

    SAC_DBG(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");

    if (!http_parse(http_buffer, &httpToken)) {
        ret = HTTP_CLOSE;
        goto EXIT;
    }

    if (!strcmp(httpToken.pToken1, "GET")) {
        SAC_DBG("get\n");
        if (!strncmp(httpToken.pToken2, "/system.htm", strlen("/system.htm"))) {
            SAC_DBG("get system.htm\n");
            send_system_page(fd);
        } else if (!strncmp(httpToken.pToken2, "/ ", 2)) {
            SAC_DBG("get /\n");
            send_system_page(fd);
        } else {
            SAC_DBG("get not found\n");
            send_http_data(fd, (char *)not_found, strlen(not_found));
            ret = HTTP_CLOSE;
        }
    } else if (!strcmp(httpToken.pToken1, "POST")) {
        SAC_DBG("post\n");
        if (!strncmp(httpToken.pToken2, "/settings.htm", strlen("/settings.htm"))) {
            SAC_DBG("post settings.htm\n");
            get_settings_post(fd, httpToken.pToken2, data_len - (httpToken.pToken2 - http_buffer));
            ret = HTTP_CLOSE;
        } else {
            SAC_DBG("post not found\n");
            send_http_data(fd, (char *)not_found, strlen(not_found));
            ret = HTTP_CLOSE;
        }
    }

EXIT:
    SAC_DBG("===============================================\n");
    return ret;
}

static int set_socket_nonblocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        goto err_exit;
    }

    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        goto err_exit;
    }
    return 0;

err_exit:
    return -1;
}

static int smart_config_http_webserver_task(void)
{
    struct sockaddr_in saddr = { 0 };
    struct sockaddr_in caddr = { 0 };
    socklen_t socklen = 0;
    int ret = 0;
    int option;

    smart_config_thread_state = TASK_RUNNING;

    smart_config_client_fd = -1;
    smart_config_server_fd = -1;

    SAC_DBG("socket begin create...\n");
    if (smart_config_server_fd < 0) {
        smart_config_server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (smart_config_server_fd < 0) {
            SAC_ERR("socket create err!\n");
            goto out;
        }

        saddr.sin_family        = AF_INET;
        saddr.sin_port          = htons(smart_config_port);
        saddr.sin_addr.s_addr   = htonl(INADDR_ANY);

        option = 1;
        ret = setsockopt(smart_config_server_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&option, sizeof(option));
        if (ret < 0) {
            SAC_ERR("failed to setsockopt sock_fd!\n");
            goto out;
        }

        /* SO_REUSEPORT is not support yet */
        option = 1;
        if ((ret = setsockopt(smart_config_server_fd, SOL_SOCKET, SO_REUSEPORT, (char *)&option, sizeof(option))) < 0) {
            SAC_ERR("failed to setsockopt sock_fd!\n");
            goto out;
        }

        ret = bind(smart_config_server_fd, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in));
        if (ret < 0) {
            SAC_ERR("Failed to bind tcp socket! errno:%d\n", errno);
            goto out;
        }

        ret = listen(smart_config_server_fd, 8);
        if (ret != 0) {
            SAC_ERR("listen failed \n");
            goto out;
        }

        /* Set the recv timeout to set the accept timeout */
#if 0
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        ret = setsockopt(smart_config_server_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (ret < 0) {
            SAC_ERR("set socket option err! errno:%d\n", errno);
            goto out;
        }
#endif

        if (set_socket_nonblocking(smart_config_server_fd)) {
            SAC_ERR("set socket nonblocking err! errno:%d\n", errno);
            goto out;
        }

        smart_config_run_state = SMART_CONFIG_START;

        SAC_DBG("socket listen success!\n");
    }

try_accept:
    SAC_DBG("try accept!\n");
    while (smart_config_run_state) {
        int rs;
        fd_set fdr;
        struct timeval tv;

        socklen = sizeof(caddr);
        while (smart_config_run_state) {
            smart_config_client_fd = accept(smart_config_server_fd, (struct sockaddr *)&caddr, &socklen);
            if (smart_config_client_fd >= 0) {
                SAC_DBG("socket accept\n");

                struct timeval timeout;
                timeout.tv_sec = 5;
                timeout.tv_usec = 0;

                setsockopt(smart_config_client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
                setsockopt(smart_config_client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

#if 0
                uint16_t client_port = ntohs(caddr.sin_port);
                char client_addr_str[32] = {0};
                inet_ntoa(caddr.sin_addr);
                SAC_DBG("client port: %u\n", client_port);
                SAC_DBG("client addr: %s\n", client_addr_str);
#endif
                break;
            } else {
                /* SAC_DBG("no client is connected.\n"); */
                usleep(100 * 1000);
            }
        }

        while (smart_config_run_state) {
            FD_ZERO(&fdr);
            FD_SET(smart_config_client_fd, &fdr);

            tv.tv_sec = 1;
            tv.tv_usec = 0;

            SAC_DBG("select().\n");
            rs = select(smart_config_client_fd + 1, &fdr, NULL, NULL, &tv);
            if (rs > 0) {
                if (FD_ISSET(smart_config_client_fd, &fdr)) {
                    rs = http_recv_request(smart_config_client_fd);
                    if (rs != 0) {
                        close(smart_config_client_fd);
                        smart_config_client_fd = -1;
                        goto try_accept;
                    }
                    switch (http_client_handle(smart_config_client_fd, strlen(http_buffer))) {
                    case HTTP_SUCCESS:
                        continue;
                    case HTTP_CLOSE:
                        close(smart_config_client_fd);
                        smart_config_client_fd = -1;
                        goto try_accept;
                    case HTTP_ERROR:
                    case HTTP_EXIT:
                    default:
                        SAC_DBG("exit!\n");
                        break;
                    }
                }
            } else if (rs < 0) {
                SAC_ERR("socket select err! errno:%d\n", errno);
                close(smart_config_client_fd);
                smart_config_client_fd = -1;
                goto try_accept;
            } else if (rs == 0) {
                continue;
            }
        }
    }

out:
    if (smart_config_server_fd >= 0) {
        close(smart_config_server_fd);
        smart_config_server_fd = -1;
    }
    if (smart_config_client_fd >= 0) {
        close(smart_config_client_fd);
        smart_config_client_fd = -1;
    }

    smart_config_thread_state = TASK_EXIT;

    SAC_DBG("task exit!\n");

    return ret;
}

static void *smart_config_task(void *arg)
{
    (void)arg;
    smart_config_http_init();

    smart_config_http_webserver_task();

    return NULL;
}

int smart_config_start(void)
{
    if (smart_config_thread_state != TASK_INIT) {
        SAC_ERR("smart config has already start!\n");
        return -1;
    }

    SAC_DBG("smart config start!\n");

    if (pthread_create(&smart_config_thread, NULL, smart_config_task, NULL) != 0) {
        perror("Failed to create thread");
        return -1;
    }

    smart_config_thread_state = TASK_CREATED;

    return 0;
}

int smart_config_stop(void)
{
    smart_config_run_state = SMART_CONFIG_STOP;

    SAC_DBG("smart config stop!\n");

    if (pthread_join(smart_config_thread, NULL) != 0) {
        perror("Failed to join thread");
        return -1;
    }

    smart_config_thread_state = TASK_INIT;

    smart_config_http_deinit();

    return 0;
}

int smart_config_get_state(void)
{
    return smart_config_run_state;
}

int smart_config_set_cb(smart_config_cb cb)
{
    smart_config_callback = cb;
    return 0;
}

int smart_config_get_result(smart_config_result *result)
{
    if (smart_config_run_state != SMART_CONFIG_COMPLETE)
        return -1;

    memcpy(result, &smart_config_data, sizeof(smart_config_result));

    return 0;
}

int smart_config_set_port(uint16_t port)
{
    if (port == 0) {
        SAC_ERR("Invalid port number: %d\n", port);
        return -1;
    }

    if (smart_config_thread_state != TASK_INIT) {
        SAC_ERR("Cannot set port while smart config is running\n");
        return -1;
    }

    smart_config_port = port;
    SAC_DBG("Port set to: %d\n", smart_config_port);
    return 0;
}
