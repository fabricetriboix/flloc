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

#define FLLOC_DISABLED
#include "flloc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>



/*------------------+
 | Macros and types |
 +------------------*/


/** Fill pattern for guard blocks */
#define FLLOC_FILL 0xa5


/** Number of records in hash table; must be 64K */
#define REC_COUNT (64* 1024)


/** A record of an allocated memory area */
struct Record {
    struct Record* next; // single linked list
    void*          real;
    size_t         size;
    const char*    file;
    int            line;
};
typedef struct Record Record;



/*------------------+
 | Global variables |
 +------------------*/


/** Global mutex */
static pthread_mutex_t gMutex = PTHREAD_MUTEX_INITIALIZER;


/** Is flloc initialised? */
static int gInitialised = 0;


/** Where to write output */
static FILE* gFile = NULL;


/** Size of the guard blocks, in bytes
 *
 * If >0, each allocated block of memory will have two buffers at the beginning
 * and the end. These two buffers will be filled in with a known pattern, and
 * they will be used to check the program didn't write outside the originally
 * allocated size.
 */
static size_t gGuardSize_B = 1024;


/** Rudimentary hash table of memory allocation records
 *
 * The key is bits 4 to 19 of the pointer as returned by `malloc()` and friends.
 * That makes 16 bits which should hopefully be more or less randomly
 * distributed.
 */
static Record gRecords[REC_COUNT];


/** Flag indicating whether memory leaks or corruptions have been detected */
static int gAllGood = 1;



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


/** Remove a record identified by its key from the hash table
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


/** Allocate memory
 *
 * This function allocates (or re-allocates if `old` is not NULL) memory. It
 * updates the hash table accordingly to keep track of allocated memory chunks.
 *
 * @param p [in] Pointer to memory to re-allocate; may be NULL
 * @param size [in] Number of bytes to allocate; may be NULL
 * @param file [in] Path to source file; may be NULL
 * @param line [in] Line in above source file; may be <=0
 *
 * @return Pointer usable by the caller, or NULL if failed uto allocate
 */
static void* doRealloc(void* old, size_t size, const char* file, int line);


/** Initialise the guard buffers if applicable */
static void fillGuard(Record* rec);


/** Check for signs of corruption in the guard buffers */
static void checkForCorruption(Record* rec);


/** Function to be run at the very end to check for memory leaks */
static void fllocCheck(void);



/*------------------------------------+
 | Implementation of public functions |
 +------------------------------------*/


void* FllocMalloc(size_t size, const char* file, int line)
{
    pthread_mutex_lock(&gMutex);
    initIfNeeded();
    void* ptr = doRealloc(NULL, size, file, line);
    pthread_mutex_unlock(&gMutex);
    return ptr;
}


void* FllocCalloc(size_t nmemb, size_t mbsize, const char* file, int line)
{
    pthread_mutex_lock(&gMutex);
    initIfNeeded();
    size_t size = nmemb * mbsize;
    void* ptr = doRealloc(NULL, size, file, line);
    if (ptr != NULL) {
        // NB: `calloc(3)` is supposed to initialise the memory to 0
        memset(ptr, 0, size);
    }
    pthread_mutex_unlock(&gMutex);
    return ptr;
}


void* FllocRealloc(void* old, size_t size, const char* file, int line)
{
    pthread_mutex_lock(&gMutex);
    initIfNeeded();
    void* ptr = doRealloc(old, size, file, line);
    pthread_mutex_unlock(&gMutex);
    return ptr;
}


void FllocFree(void* ptr, const char* file, int line)
{
    if (NULL == ptr) {
        return;
    }
    pthread_mutex_lock(&gMutex);
    initIfNeeded();
    Record* rec = recordRemove(ptr - gGuardSize_B);
    if (NULL == rec) {
        fprintf(stderr, "FLLOC FATAL: Unknown pointer %p when freeing memory\n",
                ptr);
        abort();
    }
    checkForCorruption(rec);
    free(rec->real);
    free(rec);
    pthread_mutex_unlock(&gMutex);
}


char* FllocStrdup(const char* s, const char* file, int line)
{
    if (NULL == s) {
        fprintf(stderr, "FLLOC FATAL: strdup() called with NULL argument\n");
        abort();
    }
    size_t size = strlen(s);
    char* str = FllocMalloc(size + 1, file, line);
    if (str != NULL) {
        strcpy(str, s);
    }
    return str;
}


char* FllocStrndup(const char* s, size_t n, const char* file, int line)
{
    if ((NULL == s) && (n > 0)) {
        fprintf(stderr, "FLLOC FATAL: strndup() called with NULL argument "
                "and >0 length\n");
        abort();
    }
    if ((s != NULL) && (strlen(s) < n)) {
        n = strlen(s);
    }
    char* str = FllocMalloc(n + 1, file, line);
    if (str != NULL) {
        strncpy(str, s, n);
        str[n] = '\0';
    }
    return str;
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
    uint16_t index = ptr2index(rec->real);
    rec->next = NULL;
    Record* curr = &(gRecords[index]);
    while (curr->next != NULL) {
        curr = curr->next;
    }
    curr->next = rec;
}


static Record* recordRemove(void* real)
{
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
    return rec;
}


static void initIfNeeded(void)
{
    if (gInitialised) {
        return;
    }
    gInitialised = 1;
    gFile = stderr;

    memset(gRecords, 0, sizeof(gRecords));
    atexit(fllocCheck);

    const char* str = getenv("FLLOC_CONFIG");
    if (str != NULL) {
        char* s = strdup(str);
        if (NULL == s) {
            fprintf(stderr, "FLLOC FATAL: critical strdup() failed\n");
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
}


static void parseConfig(const char* name, const char* value)
{
    if (strcmp(name, "FILE") == 0) {
        if ((gFile != NULL) && (gFile != stderr)) {
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


static void* doRealloc(void* old, size_t size, const char* file, int line)
{
    if (0 == size) {
        return NULL;
    }

    size_t capacity = size + (2 * gGuardSize_B);
    void* real = malloc(capacity);
    if (NULL == real) {
        return NULL;
    }

    Record* rec = malloc(sizeof(*rec));
    if (NULL == rec) {
        free(real);
        return NULL;
    }

    rec->real = real;
    rec->size = size;
    rec->file = file;
    rec->line = line;
    fillGuard(rec);
    recordInsert(rec);

    void* ptr = NULL;
    if (real != NULL) {
        ptr = real + gGuardSize_B;
    }

    if (old != NULL) {
        rec = recordRemove(old - gGuardSize_B);
        if (NULL == rec) {
            fprintf(stderr,
                    "FLLOC FATAL: Unknown pointer %p when doing reallocation\n",
                    old);
            abort();
        }
        checkForCorruption(rec);
        if (rec->size < size) {
            size = rec->size;
        }
        memcpy(ptr, old, size);
        free(rec->real);
        free(rec);
    }
    return ptr;
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
    size_t i;
    uint8_t* p = rec->real;
    for (i = 0; i < gGuardSize_B; i++) {
        if (*p != FLLOC_FILL) {
            fprintf(gFile, "FLLOC: Corruption detected at %p, "
                    "from block allocated at %s:%d\n",
                    p, rec->file, rec->line);
            gAllGood = 0;
            return;
        }
        p++;
    }
    p = rec->real + gGuardSize_B + rec->size;
    for (i = 0; i < gGuardSize_B; i++) {
        if (*p != FLLOC_FILL) {
            fprintf(gFile, "FLLOC: Corruption detected at %p, "
                    "from block allocated at %s:%d\n",
                    p, rec->file, rec->line);
            gAllGood = 0;
            return;
        }
        p++;
    }
}


static void fllocCheck(void)
{
    pthread_mutex_lock(&gMutex);
    int i;
    for (i = 0; i < REC_COUNT; i++) {
        Record* rec = gRecords[i].next;
        while (rec != NULL) {
            checkForCorruption(rec);
            fprintf(gFile, "FLLOC: Memory leak detected: %p never freed; "
                    "allocated from %s:%d\n",
                    rec->real + gGuardSize_B, rec->file, rec->line);
            rec = rec->next;
            gAllGood = 0;
        }
    }
    pthread_mutex_unlock(&gMutex);
    if (gAllGood) {
        fprintf(gFile, "FLLOC: No memory leaks nor corruptions detected\n");
    }
}
