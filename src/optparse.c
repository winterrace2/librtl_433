/**
* heavily reduced version of Christian Zuckschwerdts option parsing functions
*
* Only following 2 functions are used to parse the rtl_tcp query string.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "optparse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *arg_param(char *arg)
{
	char *p = strchr(arg, ':');
	if (p)
		return ++p;
	else
		return p;
}

int hostport_param(char *param, char **host, char **port)
{
	if (param && *param) {
		if (*param != ':') {
			*host = param;
			if (*param == '[') {
				(*host)++;
				param = strchr(param, ']');
				if (param) {
					*param++ = '\0';
				}
				else return 0; // exit(1); // handled at caller
			}
		}
		param = strchr(param, ':');
		if (param) {
			*param++ = '\0';
			*port = param;
		}
	}
	return 1;
}
