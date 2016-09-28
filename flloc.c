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
    EV_BADFREE,
    EV_STRDUP,
    EV_STRNDUP,
    EV_PLOUGH,
    EV_FAIL,
    EV_USER
} Event;


/** A record of an allocated memory area */
struct Record {
    struct Record* next; // single linked list
    void*          real;
    unsigned long  size;
};
typedef struct Record Record;



/*------------------+
 | Global variables |
 +------------------*/


static int gInitialised = 0;


/** Mutex for writing into the file */
static pthread_mutex_t gFileMutex = PTHREAD_MUTEX_INITIALIZER;


/** Where to write logs */
static FILE* gFile = NULL;


/** Size of the guard blocks, in bytes
 *
 * If >0, each allocated block of memory will have two buffers at the beginning
 * and the end. These two buffers will be filled in with a known pattern, and
 * they will be used to check the program didn't write outside the originally
 * allocated size.
 */
static unsigned long gGuardSize_B = 0;


/** Rudimentary hash table of memory allocation records
 *
 * The key is bits 4 to 19 of the pointer as returned by `malloc()` and friends.
 * That makes 16 bits which should hopefully be more or less randomly
 * distributed.
 */
static Record gRecords[64* 1024];


/** Mutex to protect the above hash table */
static pthread_mutex_t gRecMutex = PTHREAD_MUTEX_INITIALIZER;



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Insert a record into the hash table
 *
 * The key used is `rec->real`.
 *
 * @param rec [in,out] Record to insert
 */
static void recordInsert(Record* rec);


/** Remove a record identified by its key
 *
 * @param real [in] Key identifying the record to delete
 *
 * @return The removed record, or NULL if not found
 */
static Record* recordRemove(void* real);


/** Initialise flloc if not done already
 *
 * Calling this function multiple times is harmless.
 */
static void initIfNeeded(void);


/** Act on a configuration parameter */
static void parseConfig(const char* name, const char* value);


/** Initialise the guard buffers if applicable */
static void fillGuard(Record* rec);


/** Check for signs of gardening in the guard buffers */
static void checkForCorruption(Record* rec);


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

    unsigned long recsize = sizeof(Record);
    Record* rec = malloc(recsize);
    if (NULL == rec) {
        log(EV_FAIL, file, line, "size=%lu(%lu)", recsize, recsize);
        return NULL;
    }
    unsigned long capacity = size + (2 * gGuardSize_B);
    rec->size = size;
    rec->real = malloc(capacity);
    if (NULL == rec->real) {
        log(EV_FAIL, file, line, "size=%lu(%lu)", rec->size, capacity);
        free(rec);
        return NULL;
    }
    fillGuard(rec);
    recordInsert(rec);

    void* ptr = rec->real + gGuardSize_B;
    log(EV_MALLOC, file, line, "ptr=%p (%p) size=%lu (%lu)",
            ptr, rec->real, rec->size, capacity);
    return ptr;
}


void* FllocCalloc(size_t nmemb, size_t mbsize, const char* file, int line)
{
    initIfNeeded();

    unsigned long recsize = sizeof(Record);
    Record* rec = malloc(recsize);
    if (NULL == rec) {
        log(EV_FAIL, file, line, "size=%lu(%lu)", recsize, recsize);
        return NULL;
    }
    rec->size = nmemb * mbsize;
    unsigned long capacity = rec->size + (2 * gGuardSize_B);
    rec->real = malloc(capacity);
    if (NULL == rec->real) {
        log(EV_FAIL, file, line, "size=%lu(%lu)", rec->size, capacity);
        free(rec);
        return NULL;
    }
    // NB: `calloc(3)` is supposed to initialise the memory to 0
    memset(rec->real + gGuardSize_B, 0, rec->size);
    fillGuard(rec);
    recordInsert(rec);

    void* ptr = rec->real + gGuardSize_B;
    log(EV_CALLOC, file, line, "ptr=%p (%p) size=%lu (%lu) nmemb=%lu mbsize=%lu",
            ptr, rec->real, rec->size, capacity,
            (unsigned long)nmemb, (unsigned long)mbsize);
    return ptr;
}


void* FllocRealloc(void* p, size_t size, const char* file, int line)
{
    initIfNeeded();

    unsigned long recsize = sizeof(Record);
    Record* rec = malloc(recsize);
    if (NULL == rec) {
        log(EV_FAIL, file, line, "size=%lu(%lu)", recsize, recsize);
        return NULL;
    }
    unsigned long capacity = size + (2 * gGuardSize_B);
    rec->size = size;
    rec->real = realloc(p, capacity);
    if (NULL == rec->real) {
        log(EV_FAIL, file, line, "size=%lu(%lu)", rec->size, capacity);
        free(rec);
        return NULL;
    }
    fillGuard(rec);
    recordInsert(rec);

    void* ptr = rec->real + gGuardSize_B;
    log(EV_REALLOC, file, line, "ptr=%p (%p) size=%lu (%lu)",
            ptr, rec->real, rec->size, capacity);
    return ptr;
}


void FllocFree(void* ptr, const char* file, int line)
{
    if (NULL == ptr) {
        return;
    }
    initIfNeeded();

    void* real = ptr - gGuardSize_B;
    Record* rec = recordRemove(real);
    if (NULL == rec) {
        log(EV_BADFREE, file, line, "ptr=%p(%p)", ptr, real);
    } else {
        checkForCorruption(rec);
        log(EV_FREE, file, line, "ptr=%p (%p)", ptr, real);
    }
    free(real);
    free(rec);
}


char* FllocStrdup(const char* s, const char* file, int line)
{
    if (NULL == s) {
        abort();
    }
    initIfNeeded();

    unsigned long recsize = sizeof(Record);
    Record* rec = malloc(recsize);
    if (NULL == rec) {
        log(EV_FAIL, file, line, "size=%lu(%lu)", recsize, recsize);
        return NULL;
    }
    rec->size = strlen(s) + 1;
    unsigned long capacity = rec->size + (2 * gGuardSize_B);
    rec->real = malloc(capacity);
    if (NULL == rec->real) {
        log(EV_FAIL, file, line, "size=%lu(%lu)", rec->size, capacity);
        free(rec);
        return NULL;
    }
    fillGuard(rec);
    recordInsert(rec);

    char* ptr = rec->real + gGuardSize_B;
    log(EV_STRDUP, file, line, "ptr=%p (%p) size=%lu (%lu)",
            ptr, rec->real, rec->size, capacity);
    strcpy(ptr, s);
    return ptr;
}


char* FllocStrndup(const char* s, size_t n, const char* file, int line)
{
    if ((NULL == s) && (n > 0)) {
        abort();
    }
    initIfNeeded();

    unsigned long recsize = sizeof(Record);
    Record* rec = malloc(recsize);
    if (NULL == rec) {
        log(EV_FAIL, file, line, "size=%lu(%lu)", recsize, recsize);
        return NULL;
    }
    rec->size = strlen(s);
    if (rec->size > n) {
        rec->size = n;
    }
    rec->size++;
    unsigned long capacity = rec->size + (2 * gGuardSize_B);
    rec->real = malloc(capacity);
    if (NULL == rec->real) {
        log(EV_FAIL, file, line, "size=%lu(%lu)", rec->size, capacity);
        free(rec);
        return NULL;
    }
    fillGuard(rec);
    recordInsert(rec);

    char* ptr = rec->real + gGuardSize_B;
    log(EV_STRNDUP, file, line, "ptr=%p (%p) size=%lu (%lu)",
            ptr, rec->real, rec->size, capacity);
    strncpy(ptr, s, rec->size);
    ptr[rec->size - 1] = '\0';
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


static inline uint16_t ptr2index(void* real)
{
    return ((uint32_t)real) >> 4;
}


static void recordInsert(Record* rec)
{
    pthread_mutex_lock(&gRecMutex);

    uint16_t index = ptr2index(rec->real);
    rec->next = NULL;
    Record* curr = &(gRecords[index]);
    while (curr->next != NULL) {
        curr = curr->next;
    }
    curr->next = rec;

    pthread_mutex_unlock(&gRecMutex);
}


#if 0
static const Record* recordLookup(void* real)
{
    pthread_mutex_lock(&gRecMutex);

    uint16_t index = ptr2index(real);
    Record* rec = NULL;
    Record* curr = gRecords[index].next;
    while ((curr != NULL) && (NULL == rec)) {
        if (curr->real == real) {
            rec = curr;
        } else {
            curr = curr->next;
        }
    }

    pthread_mutex_unlock(&gRecMutex);
    return rec;
}
#endif


static Record* recordRemove(void* real)
{
    pthread_mutex_lock(&gRecMutex);

    uint16_t index = ptr2index(real);
    Record* rec = NULL;
    Record* curr = &(gRecords[index]);
    while ((curr->next != NULL) && (NULL == rec)) {
        if (curr->next->real == real) {
            rec = curr->next;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }

    pthread_mutex_unlock(&gRecMutex);
    return rec;
}


static void initIfNeeded(void)
{
    if (gInitialised) {
        return;
    }
    gInitialised = 1;

    memset(gRecords, 0, sizeof(gRecords));

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


static void fillGuard(Record* rec)
{
    if (gGuardSize_B > 0) {
        memset(rec->real, FLLOC_FILL, gGuardSize_B);
        memset(rec->real + gGuardSize_B + rec->size, FLLOC_FILL, gGuardSize_B);
    }
}


static void checkForCorruption(Record* rec)
{
    unsigned long i;
    uint8_t* p = rec->real;
    for (i = 0; i < gGuardSize_B; i++) {
        if (*p != FLLOC_FILL) {
            log(EV_PLOUGH, NULL, 0, "ptr=%p(%p) at=%p",
                    rec->real + gGuardSize_B, rec->real, p);
            return;
        }
        p++;
    }
    p = rec->real + gGuardSize_B + rec->size;
    for (i = 0; i < gGuardSize_B; i++) {
        if (*p != FLLOC_FILL) {
            log(EV_PLOUGH, NULL, 0, "ptr=%p(%p) at=%p",
                    rec->real + gGuardSize_B, rec->real, p);
            return;
        }
        p++;
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
        case EV_BADFREE : name = "BADFREE"; break;
        case EV_STRDUP  : name = "STRDUP"; break;
        case EV_STRNDUP : name = "STRNDUP"; break;
        case EV_PLOUGH  : name = "PLOUGH"; break;
        case EV_FAIL    : name = "NAME"; break;
        case EV_USER    : name = "USER"; break;
    }
    if (NULL == file) {
        file = "(null)";
    }
    pthread_mutex_lock(&gFileMutex);
    fprintf(gFile, "%s [%s:%d] ", name, file, line);
    vfprintf(gFile, format, ap);
    fprintf(gFile, "\n");
    pthread_mutex_unlock(&gFileMutex);
}
