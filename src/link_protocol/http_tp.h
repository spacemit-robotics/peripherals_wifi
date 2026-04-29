/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * HTTP Response Templates
 *
 * This file contains HTTP response templates and HTML pages for the AP config server.
 */

#ifndef HTTP_TP_H
#define HTTP_TP_H

/* HTTP Response Headers */
extern const char headerPage[];
extern const char HTTPSaveResponse[];

/* HTTP Status Responses */
extern const char authrized[];
extern const char not_found[];

/* HTML Pages */
extern const char systemPage[];
extern const char SaveResponseSucc[];
extern const char SaveResponseError[];

#endif /* HTTP_TP_H */
