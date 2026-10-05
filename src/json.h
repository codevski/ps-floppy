/* Just enough JSON for flat config objects and small API replies. */
#pragma once

#include <stddef.h>

/* Copy the string value of top-level `key` into `out`. Returns 0 if found,
 * -1 if the key is missing, not a string, or does not fit. */
int json_get_string(const char *json, const char *key, char *out, size_t size);

/* Write `in` to `out` as the inside of a JSON string (no quotes added).
 * Returns 0, or -1 if it does not fit. */
int json_escape(const char *in, char *out, size_t size);

/* Navigation over a whole document, for Floppy's nested replies. A value is a
 * pointer to its first character inside the document.
 *
 * Call json_valid first. The functions below return NULL both for "absent"
 * and for "malformed", and only a valid document makes NULL mean absent. A
 * reply cut short by the client's buffer is not valid.
 *
 * Every function accepts NULL for its value and treats it as absent, so
 * lookups chain: json_member(json_member(doc, "a"), "b"). */

/* 0 if `doc` is exactly one well-formed JSON value, -1 otherwise. */
int json_valid(const char *doc);

/* Just past the value at `v`, or NULL if it is malformed. */
const char *json_skip(const char *v);

/* The value of member `key` in the object at `obj`, or NULL. */
const char *json_member(const char *obj, const char *key);

/* The first element of the array at `arr`, or NULL if it is empty. */
const char *json_first(const char *arr);

/* The element after `elem` in its array, or NULL at the end. */
const char *json_next(const char *elem);

/* Decode the string at `v` into `out`. Returns 0, or -1 if `v` is not a
 * string or does not fit. */
int json_string(const char *v, char *out, size_t size);

/* Read the whole number at `v`. Returns 0, or -1 if `v` is not an integer
 * (fractions and exponents included) or does not fit in a long. */
int json_long(const char *v, long *out);
