/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * HTTP Response Templates - Implementation
 */

#include "http_tp.h"

const char headerPage[] =
    "HTTP/1.1 200 OK\r\n"
    "Server: MySocket Server\r\n"
    "Date: TEST\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: %d\r\n"
    "Connection: close\r\n"
    "Accept-Ranges: bytes\r\n\r\n";

const char HTTPSaveResponse[] =
    "HTTP/1.1 200 OK\r\n"
    "Server: MySocket Server\r\n"
    "Date: TEST\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: %d\r\n"
    "Accept-Ranges: bytes\r\n"
    "Connection: close\r\n\r\n"
    "%s";

const char authrized[] =
    "HTTP/1.1 401 Authorization Required\r\n"
    "Server: MySocket Server\r\n"
    "WWW-Authenticate: Basic realm=\"SPACEMIT\"\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: 169\r\n\r\n"
    "<HTML>\r\n<HEAD>\r\n<TITLE>Error</TITLE>\r\n"
    "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=ISO-8859-1\">\r\n"
    "</HEAD>\r\n<BODY><H1>401 Unauthorized.</H1></BODY>\r\n</HTML>";

const char not_found[] =
    "HTTP/1.1 200 OK\r\n"
    "Server: MySocket Server\r\n"
    "Content-Length: 145\r\n"
    "Connection: close\r\n"
    "Content-Type: text/html\r\n\r\n"
    "<html><head><title>404 Not Found</title></head><body>\r\n"
    "<h1>Not Found</h1>\r\n"
    "<p>The requested URL was not found on this server.</p>\r\n"
    "</body></html>";

const char systemPage[] =
    "<html><head><title>Spacemit SmartConfig</title>\r\n"
    "</head>\r\n"
    "<body>"
    "<br /><font size=\"6\" color=\"red\">Spacemit SmartConfig</font><br /><br />\r\n"
    "<br />Firmware Version:&nbsp;%s&nbsp;<br />\r\n"
    "<form action=\"settings.htm\" method=\"post\">\r\n"
    "<table id=\"displayme\" border=\"0\" width=\"500\" cellspacing=\"2\">\r\n"
    "<col align=\"right\" /> <col align=\"left\" />\r\n"
    "<tbody><tr><td>SSID:&nbsp;</td>\r\n"
    "<td><Input type=\"text\" name=\"SSID\" value = \"%s\"/></td></tr>\r\n"
    "<tr><td>Key:&nbsp;</td> <td><Input type=\"text\" name=\"PSK\" value= \"%s\"/></td></tr>\r\n"
    "</tbody></table><br />\r\n"
    "<INPUT type=\"submit\" name=\"save\" value=\"Save\"><br />"
    "</FORM>"
    "</body></html>\r\n";

const char SaveResponseSucc[] =
    "<html>\r\n"
    "<head>\r\n"
    "<title>SPACEMIT Wi-Fi module</title>\r\n"
    "</head>\r\n"
    "<body>\r\n"
    "<p>Save Config Done!<a href=\"/system.htm\">Return</a></p>\r\n"
    "</body>\r\n"
    "</html>";

const char SaveResponseError[] =
    "<html>\r\n"
    "<head>\r\n"
    "<title>SPACEMIT Wi-Fi module</title>\r\n"
    "</head>\r\n"
    "<body>\r\n"
    "<p>Save Config Error, please retry<a href=\"/system.htm\">Return</a></p>\r\n"
    "</body>\r\n"
    "</html>";
