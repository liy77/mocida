/**
 * @file extra.h
 * A file used to define extra functions and macros for the UI toolkit.
 */

#ifndef UI_EXTRA_H
#define UI_EXTRA_H
#include <stdio.h>
#include <string.h>

/**
 * Checks whether the given string is a syntactically valid HTTP/HTTPS
 * URL. Performs no I/O - only inspects the prefix and shape.
 *
 * @param url String to validate.
 * @return 1 when it looks like a valid URL, 0 otherwise.
 */
int MOCIDA_IsValidURL(const char* url);

#endif // UI_EXTRA_H