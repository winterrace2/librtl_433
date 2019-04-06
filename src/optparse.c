/** @file
    reduced version of Christian Zuckschwerdts option parsing functions
    Following functions are used to parse the rtl_tcp query string.
 
    Option parsing functions to complement getopt.

    Copyright (C) 2017 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "optparse.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

int atobv(char *arg, int def)
{
    if (!arg)
        return def;
    if (!strcasecmp(arg, "true") || !strcasecmp(arg, "yes") || !strcasecmp(arg, "on") || !strcasecmp(arg, "enable"))
        return 1;
    return atoi(arg);
}

char *arg_param(char *arg)
{
    if (!arg)
        return NULL;
    char *p = strchr(arg, ':');
    char *c = strchr(arg, ',');
    if (p && (!c || p < c))
        return ++p;
    else if (c)
        return c;
    else
        return p;
}

char *hostport_param(char *param, char **host, char **port)
{
    if (param && *param) {
        if (param[0] == '/' && param[1] == '/') {
            param += 2;
        }
        if (*param != ':' && *param != ',') {
            *host = param;
            if (*param == '[') {
                (*host)++;
                param = strchr(param, ']');
                if (param) {
                    *param++ = '\0';
                }
                else return NULL; // exit(1); // handled at caller
            }
        }
        char *colon = strchr(param, ':');
        char *comma = strchr(param, ',');
        if (colon && (!comma || colon < comma)) {
            *colon++ = '\0';
            *port    = colon;
        }
        if (comma) {
            *comma++ = '\0';
            return comma;
        }
    }
    return "";
}

char *trim_ws(char *str)
{
    if (!str || !*str)
        return str;
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n')
        ++str;
    char *e = str; // end pointer (last non ws)
    char *p = str; // scanning pointer
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            ++p;
        if (*p)
            e = p++;
    }
    *++e = '\0';
    return str;
}

char *remove_ws(char *str)
{
    if (!str)
        return str;
    char *d = str; // dst pointer
    char *s = str; // src pointer
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
            ++s;
        if (*s)
            *d++ = *s++;
    }
    *d++ = '\0';
    return str;
}
