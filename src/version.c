/*
** Version information for the extensible SQL parser library.
** This string is the single source of truth for the library's
** version; lime.c mirrors it via LIME_VERSION_STRING and
** meson.build via project(version: ...).  Keep the three in sync.
*/
#include "parser.h"

#ifndef LIME_VERSION_STRING
#define LIME_VERSION_STRING "0.9.1"
#endif

const char *lime_parser_version(void) {
    return LIME_VERSION_STRING;
}
