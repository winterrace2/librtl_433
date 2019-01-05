/**
 * Generic list
 *
 * Copyright (C) 2018 Christian Zuckschwerdt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "list.h"
#include <stdlib.h>

RTL_433_API void list_initialize(list_t *list)
{
	list->elems = NULL;
	list->size = 0;
	list->len = 0;
}

RTL_433_API void list_ensure_size(list_t *list, size_t min_size)
{
    if (!list->elems || list->size < min_size) {
        list->elems = realloc(list->elems, min_size * sizeof(*list->elems));
        list->size  = min_size;

        list->elems[list->len] = NULL; // ensure a terminating NULL
    }
}

RTL_433_API void list_push(list_t *list, void *p)
{
    if (list->len + 1 >= list->size) // account for terminating NULL
        list_ensure_size(list, list->size < 8 ? 8 : list->size + list->size / 2);

    list->elems[list->len++] = p;

    list->elems[list->len] = NULL; // ensure a terminating NULL
}

RTL_433_API void list_push_all(list_t *list, void **p)
{
	for (void **iter = p; iter && *iter; ++iter)
		list_push(list, *iter);
}

RTL_433_API void list_clear(list_t *list, list_elem_free_fn elem_free)
{
    if (elem_free) {
        for (size_t i = 0; i < list->len; ++i) { // list might contain NULLs
            elem_free(list->elems[i]);
        }
    }
    list->len = 0;
    if (list->elems) {
        list->elems[0] = NULL; // ensure a terminating NULL
    }
}

RTL_433_API void list_free_elems(list_t *list, list_elem_free_fn elem_free)
{
	list_clear(list, elem_free);
	free(list->elems);
	list->elems = NULL;
	list->size = 0;
}
