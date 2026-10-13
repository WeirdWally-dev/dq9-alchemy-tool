/* MSVC has no <strings.h>; gumbo only needs these two from it. */
#pragma once
#include <string.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
