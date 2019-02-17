/*
 * A general structure for extracting hierarchical data from the devices;
 * typically key-value pairs, but allows for more rich data as well.
 *
 * Copyright (C) 2015 by Erkki Seppälä <flux@modeemi.fi>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <limits.h>

#include "abuf.h"
#include "redir_print.h"
// gethostname() needs _XOPEN_SOURCE 500 on unistd.h
#define _XOPEN_SOURCE 500

#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
  #if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0600)
  #undef _WIN32_WINNT
  #define _WIN32_WINNT 0x0600   /* Needed to pull in 'struct sockaddr_storage' */
  #endif

  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <netdb.h>
  #include <netinet/in.h>

  #define SOCKET          int
  #define INVALID_SOCKET  -1
#endif

#include <time.h>

#include "data.h"

#ifdef _WIN32
  #define _POSIX_HOST_NAME_MAX  128
  #undef  close   /* We only work with sockets here */
  #define close(s)              closesocket (s)
  #define perror(str)           ws2_perror (str)

  static void ws2_perror (const char *str)
  {
    if (str && *str)
        rtl433_fprintf(stderr, "%s: ", str);
    rtl433_fprintf(stderr, "Winsock error %d.\n", WSAGetLastError());
  }
#endif

/* Datagram (UDP) client */

typedef struct {
    struct sockaddr_storage addr;
    socklen_t addr_len;
    SOCKET sock;
} datagram_client_t;

static int datagram_client_open(datagram_client_t *client, const char *host, const char *port)
{
    if (!host || !port)
        return -1;

    struct addrinfo hints, *res, *res0;
    int    error;
    SOCKET sock;
    const char *cause = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_ADDRCONFIG;
    error = getaddrinfo(host, port, &hints, &res0);
    if (error) {
        rtl433_fprintf(stderr, "%s\n", gai_strerror(error));
        return -1;
    }
    sock = INVALID_SOCKET;
    for (res = res0; res; res = res->ai_next) {
        sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock >= 0) {
            client->sock = sock;
            memset(&client->addr, 0, sizeof(client->addr));
            memcpy(&client->addr, res->ai_addr, res->ai_addrlen);
            client->addr_len = res->ai_addrlen;
            break; // success
        }
    }
    freeaddrinfo(res0);
    if (sock == INVALID_SOCKET) {
        perror("socket");
        return -1;
    }

    //int broadcast = 1;
    //int ret = setsockopt(client->sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    return 0;
}

static void datagram_client_close(datagram_client_t *client)
{
    if (!client)
        return;

    if (client->sock != INVALID_SOCKET) {
        close(client->sock);
        client->sock = INVALID_SOCKET;
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

static void datagram_client_send(datagram_client_t *client, const char *message, size_t message_len)
{
    int r =  sendto(client->sock, message, message_len, 0, (struct sockaddr *)&client->addr, client->addr_len);
    if (r == -1) {
        perror("sendto");
    }
}

/* Syslog UDP printer, RFC 5424 (IETF-syslog protocol) */

typedef struct {
    data_output_t output;
    datagram_client_t client;
    int pri;
    char hostname[_POSIX_HOST_NAME_MAX + 1];
    abuf_t msg;
} data_output_syslog_t;

static void print_syslog_array(data_output_t *output, data_array_t *array, char *format)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;

    abuf_cat(&syslog->msg, "[");
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            abuf_cat(&syslog->msg, ",");
        print_array_value(output, array, format, c);
    }
    abuf_cat(&syslog->msg, "]");
}

static void print_syslog_object(data_output_t *output, data_t *data, char *format)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;

    bool separator = false;
    abuf_cat(&syslog->msg, "{");
    while (data) {
        if (separator)
            abuf_cat(&syslog->msg, ",");
        output->print_string(output, data->key, NULL);
        abuf_cat(&syslog->msg, ":");
        print_value(output, data->type, data->value, data->format);
        separator = true;
        data = data->next;
    }
    abuf_cat(&syslog->msg, "}");
}

static void print_syslog_data(data_output_t *output, data_t *data, char *format)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;

    if (syslog->msg.tail) {
        print_syslog_object(output, data, format);
        return;
    }

    char message[1024];
    abuf_init(&syslog->msg, message, 1024);

    time_t now;
    struct tm tm_info;
    time(&now);
#ifdef _WIN32
    gmtime_s(&tm_info, &now);
#else
    gmtime_r(&now, &tm_info);
#endif
    char timestamp[21];
    strftime(timestamp, 21, "%Y-%m-%dT%H:%M:%SZ", &tm_info);

    abuf_printf(&syslog->msg, "<%d>1 %s %s rtl_433 - - - ", syslog->pri, timestamp, syslog->hostname);

    print_syslog_object(output, data, format);

    datagram_client_send(&syslog->client, message, strlen(message));

    abuf_setnull(&syslog->msg);
}

static void print_syslog_string(data_output_t *output, const char *str, char *format)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;

    char *buf = syslog->msg.tail;
    size_t size = syslog->msg.left;

    if (size < strlen(str) + 3) {
        return;
    }

    *buf++ = '"';
    size--;
    for (; *str && size >= 3; ++str) {
        if (*str == '"' || *str == '\\') {
            *buf++ = '\\';
            size--;
        }
        *buf++ = *str;
        size--;
    }
    if (size >= 2) {
        *buf++ = '"';
        size--;
    }
    *buf = '\0';

    syslog->msg.tail = buf;
    syslog->msg.left = size;
}

static void print_syslog_double(data_output_t *output, double data, char *format)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;
    abuf_printf(&syslog->msg, "%f", data);
}

static void print_syslog_int(data_output_t *output, int data, char *format)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;
    abuf_printf(&syslog->msg, "%d", data);
}

static void data_output_syslog_free(data_output_t *output)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;

    if (!syslog)
        return;

    datagram_client_close(&syslog->client);

    free(syslog);

#ifdef _WIN32
	if(WSACleanup() != 0) {
		perror("WSAStartup()");
	}
#endif
}

data_output_t *data_output_syslog_create(const char *host, const char *port)
{
    data_output_syslog_t *syslog = calloc(1, sizeof(data_output_syslog_t));
    if (!syslog) {
        rtl433_fprintf(stderr, "calloc() failed");
        return NULL;
    }
#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2,2),&wsa) != 0) {
        perror("WSAStartup()");
        free(syslog);
        return NULL;
    }
#endif

	syslog->output.print_data   = print_syslog_data;
    syslog->output.print_array  = print_syslog_array;
    syslog->output.print_string = print_syslog_string;
    syslog->output.print_double = print_syslog_double;
    syslog->output.print_int    = print_syslog_int;
    syslog->output.output_free  = data_output_syslog_free;
	syslog->output.file = NULL;
	syslog->output.ext_callback = NULL; // prevents this printer to receive unknown signals
	// Severity 5 "Notice", Facility 20 "local use 4"
    syslog->pri = 20 * 8 + 5;
    gethostname(syslog->hostname, _POSIX_HOST_NAME_MAX + 1);
    syslog->hostname[_POSIX_HOST_NAME_MAX] = '\0';
    datagram_client_open(&syslog->client, host, port);

    return &syslog->output;
}
