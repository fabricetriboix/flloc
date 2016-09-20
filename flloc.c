/* Copyright (c) 2016  Fabrice Triboix
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#undef FLLOC_ENABLED
#include "flloc.h"
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>



/*------------------+
 | Macros and types |
 +------------------*/


/** Fill pattern for guard blocks */
#define FLLOC_FILL 0xa5


/** The different types of things to log */
typedef enum {
    EV_MALLOC,
    EV_CALLOC,
    EV_REALLOC,
    EV_FREE,
    EV_STRDUP,
    EV_STRNDUP,
    EV_USER
} Event;



/*------------------+
 | Global variables |
 +------------------*/


/** Mutex for writing into the file */
static pthread_mutex_t gMutex = PTHREAD_MUTEX_INITIALIZER;


/** Where to write logs */
static FILE* gFile = NULL;


/** Size of the guard blocks, in bytes
 *
 * If >0, each allocated block of memory will have two buffers at the beggining
 * and the end. These two buffers will be filled in with a known pattern, and
 * they will be used to check the program didn't write outside the originally
 * allocated size.
 */
static size_t gGuardSize_B = 0;



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Initialise flloc if not done already
 *
 * Calling this function multiple times is harmless.
 */
static void initIfNeeded(void);


/** Act on a configuration parameter */
static void config(const char* name, const char* value);


/** Initialise the guard buffers if applicable
 *
 * @param real [in] Pointer to the memory block
 * @param size [in] Original size (i.e. without guard blocks)
 */
static void fillGuard(void* real, size_t size);


/** Check for signs of gardening in the guard buffers */
static void checkForCorruption(void* real);


/** Write a formatted string to the log file */
static void log(Event ev, const char* file, int line,
        const char* format, ...)
#ifdef __GNUC__
    __attribute__(( format(printf, 4, 5) ));
#endif
    ;


/** Write a formatted string to the log file */
static void vlog(Event ev, const char* file, int line,
        const char* format, va_list ap);



/*------------------------------------+
 | Implementation of public functions |
 +------------------------------------*/


void* FllocMalloc(size_t size, const char* file, int line)
{
    initIfNeeded();

    size_t capacity = size + (2 * gGuardSize_B);
    void* real = malloc(capacity);
    fillGuard(real, size);

    void* ptr = real;
    if (ptr != NULL) {
        ptr += gGuardSize_B;
        fillGuard(real, size);
    }
    log(EV_MALLOC, file, line, "ptr=%p (%p) size=%u (%u)",
            ptr, real, (unsigned)size, (unsigned)capacity);
    return ptr;
}


void* FllocCalloc(size_t nmemb, size_t mbsize, const char* file, int line)
{
    initIfNeeded();

    size_t size = nmemb * mbsize;
    size_t capacity = size + (2 * gGuardSize_B);
    void* real = malloc(capacity);
    if (real != NULL) {
        // NB: `calloc(3)` is supposed to initialise the memory to 0
        memset(real + gGuardSize_B, 0, size);
    }

    void* ptr = real;
    if (ptr != NULL) {
        ptr += gGuardSize_B;
        fillGuard(real, size);
    }
    log(EV_CALLOC, file, line, "ptr=%p (%p) size=%u (%u) nmemb=%u mbsize=%u",
            ptr, real, (unsigned)size, (unsigned)capacity,
            (unsigned)nmemb, (unsigned)mbsize);
    return ptr;
}


void* FllocRealloc(void* p, size_t size, const char* file, int line)
{
    initIfNeeded();

    size_t capacity = size + (2 * gGuardSize_B);
    void* real = realloc(p, capacity);

    void* ptr = real;
    if (ptr != NULL) {
        ptr += gGuardSize_B;
        fillGuard(real, size);
    }
    log(EV_REALLOC, file, line, "ptr=%p (%p) size=%u (%u)",
            ptr, real, (unsigned)size, (unsigned)capacity);
    return ptr;
}


void FllocFree(void* ptr, const char* file, int line)
{
    initIfNeeded();

    void* real = ptr;
    if (real != NULL) {
        real -= gGuardSize_B;
    }
    checkForCorruption(real);
    log(EV_FREE, file, line, "ptr=%p (%p)", ptr, real);
    free(real);
}


char* FllocStrdup(const char* s, const char* file, int line)
{
    initIfNeeded();

    if (NULL == s) {
        abort();
    }
    size_t size = strlen(s) + 1;
    size_t capacity = size + (2 * gGuardSize_B);
    char* real = malloc(capacity);
    fillGuard(real, size);

    char* ptr = real + gGuardSize_B;
    log(EV_STRDUP, file, line, "ptr=%p (%p) size=%u (%u)",
            ptr, real, (unsigned)size, (unsigned)capacity);
    strcpy(ptr, s);
    return ptr;
}


char* FllocStrndup(const char* s, size_t n, const char* file, int line)
{
    initIfNeeded();

    if ((NULL == s) && (n > 0)) {
        abort();
    }
    size_t size = strlen(s);
    if (size > n) {
        size = n;
    }
    size++;
    size_t capacity = size + (2 * gGuardSize_B);
    char* real = malloc(capacity);
    fillGuard(real, size);

    char* ptr = real + gGuardSize_B;
    log(EV_STRNDUP, file, line, "ptr=%p (%p) size=%u (%u)",
            ptr, real, (unsigned)size, (unsigned)capacity);
    strncpy(ptr, s, size);
    ptr[size - 1] = '\0';
    return ptr;
}


void FllocMsg(const char* file, int line, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    vlog(EV_USER, file, line, format, ap);
    va_end(ap);
}


void FllocVMsg(const char* file, int line, const char* format, va_list ap)
{
    vlog(EV_USER, file, line, format, ap);
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static void initIfNeeded(void)
{
    if (gFile != NULL) {
        return;
    }

    const char* str = getenv("FLLOC_CONFIG");
    if (NULL == str) {
        return;
    }

    char* s = strdup(str);
    if (NULL == s) {
        abort();
    }
    char* saveptr;
    char* token = strtok_r(s, ";", &saveptr);
    while (token != NULL) {
        char* saveptr2;
        char* name = strtok_r(token, "=", &saveptr2);
        char* value = strtok_r(NULL, "=", &saveptr2);
        if ((name != NULL) && (value != NULL)) {
            parseConfig(name, value);
        }
        token = strtok_r(NULL, ";", &saveptr);
    }
    free(s);
}


static void parseConfig(const char* name, const char* value)
{
    if (strcmp(name, "FILE") == 0) {
        if (gFile != NULL) {
            fclose(gFile);
        }
        gFile = fopen(value, "w");
        if (NULL == gFile) {
            fprintf(stderr, "FLLOC FATAL: Can't open '%s' for writing\n",
                    value);
            abort();
        }

    } else if (strcmp(name, "GUARD") == 0) {
        unsigned long tmp;
        if (sscanf(value, "%lu", &tmp) != 1) {
            fprintf(stderr, "FLLOC FATAL: Invalid GUARD value '%s'\n", value);
            abort();
        }
        gGuardSize_B = tmp;

    } else {
        fprintf(stderr, "FLLOC WARNING: Unknown parameter '%s'; ignored\n",
                name);
    }
}


static void fillGuard(void* real, size_t size)
{
    if ((real != NULL) && (gGuardSize_B > 0)) {
        memset(real, FLLOC_FILL, gGuardSize_B);
        memset(real + gGuardSize_B + size, FLLOC_FILL, gGuardSize_B);
    }
}


static void checkForCorruption(void* real)
{
    if (real != NULL) {
        size_t i;
        for (i = 0; i < gGuardSize_B; i++) {
            if ((
        }
    }
}


static void log(Event ev, const char* file, int line,
        const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    vlog(ev, file, line, format, ap);
    va_end(ap);
}


static void vlog(Event ev, const char* file, int line,
        const char* format, va_list ap)
{
    const char* name = "UNKNOWN";
    switch (ev) {
        case EV_MALLOC  : name = "MALLOC"; break;
        case EV_CALLOC  : name = "CALLOC"; break;
        case EV_REALLOC : name = "REALLLC"; break;
        case EV_FREE    : name = "FREE"; break;
        case EV_STRDUP  : name = "STRDUP"; break;
        case EV_STRNDUP : name = "STRNDUP"; break;
        case EV_USER    : name = "USER"; break;
    }
    if (NULL == file) {
        file = "(null)";
    }
    pthread_mutex_lock(&gMutex);
    fprintf(gFile, "%s [%s:%d] ", name, file, line);
    vfprintf(gFile, format, ap);
    fprintf(gFile, "\n");
    pthread_mutex_unlock(&gMutex);
}
