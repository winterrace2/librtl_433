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

#ifndef INCLUDE_LIST_H_
#define INCLUDE_LIST_H_

#include <stddef.h>
#include "librtl_433_export.h"

/// Dynamically growing list, elems is always NULL terminated, call list_ensure_size() to alloc elems.
typedef struct list {
    void **elems;
    size_t size;
    size_t len;
} list_t;

typedef void(*list_elem_free_fn)(void *);

// ensure that list object is initially empty (should be used if the used memory is not guaranteed to start with all 0-bytes)
RTL_433_API void list_initialize(list_t *list);

/// Alloc elems if needed and ensure the list has room for at least min_size elements.
RTL_433_API void list_ensure_size(list_t *list, size_t min_size);

/// Add to the end of elems, allocs or grows the list if needed and ensures the list has a terminating NULL.
RTL_433_API void list_push(list_t *list, void *p);

/// Adds all elements of a NULL terminated list to the end of elems, allocs or grows the list if needed and ensures the list has a terminating NULL.
RTL_433_API void list_push_all(list_t *list, void **p);

/// Clear the list, frees each element with fn, does not free backing or list itself.
RTL_433_API void list_clear(list_t *list, list_elem_free_fn elem_free);

/// Clear the list, free backing, does not free list itself.
RTL_433_API void list_free_elems(list_t *list, list_elem_free_fn elem_free);

#endif // INCLUDE_LIST_H_
