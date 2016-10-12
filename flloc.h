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

#ifndef FLLOC_h_
#define FLLOC_h_


#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>


/** malloc-like function
 *
 * @param size [in] As `malloc(3)`
 * @param file [in] Source file; maybe be NULL
 * @param line [in] Line number
 *
 * @return As `malloc(3)`
 */
void* FllocMalloc(size_t size, const char* file, int line);


/** calloc-like function
 *
 * @param nmemb [in] As `calloc(3)`
 * @param size  [in] As `calloc(3)`
 * @param file  [in] Source file; maybe be NULL
 * @param line  [in] Line number
 *
 * @return As `calloc(3)`
 */
void* FllocCalloc(size_t nmemb, size_t size, const char* file, int line);


/** realloc-like function
 *
 * @param ptr  [in] As `realloc(3)`
 * @param size [in] As `realloc(3)`
 * @param file  [in] Source file; maybe be NULL
 * @param line  [in] Line number
 *
 * @return As `realloc(3)`
 */
void* FllocRealloc(void* ptr, size_t size, const char* file, int line);


/** free-like function
 *
 * @param ptr  [in] As `free(3)`
 * @param file [in] Source file; maybe be NULL
 * @param line [in] Line number
 *
 * @return As `free(3)`
 */
void FllocFree(void* ptr, const char* file, int line);


/** strdup-like function
 *
 * @param s    [in] String to duplicate; must not be NULL
 * @param file [in] Source file; maybe be NULL
 * @param line [in] Line number
 *
 * @return As `strdup(3)`
 */
char* FllocStrdup(const char* s, const char* file, int line);


/** strndup-like function
 *
 * @param s    [in] String to duplicate
 * @param n    [in] Maximum number of characters to duplicate (not including
 *                  the terminating null character)
 * @param file [in] Source file; maybe be NULL
 * @param line [in] Line number
 *
 * @return As `strndup(3)`
 */
char* FllocStrndup(const char* s, size_t n, const char* file, int line);


/** Print a message in the log file */
#define FllocPrintf(_format, ...) \
        FllocMsg(__FILE__, __LINE__, (_format), ## __VA_ARGS__)


/** Print a message in the log file (va-arg style) */
#define FllocVPrintf(_format, ap) \
        FllocVMsg(__FILE__, __LINE__, (_format), (ap))


void FllocMsg(const char* file, int line, const char* format, ...)
#ifdef __GNUC__
	__attribute__ (( format(printf, 3, 4) ))
#endif
;


void FllocVMsg(const char* file, int line, const char* format, va_list ap);


#ifndef FLLOC_DISABLED

#ifdef malloc
#undef malloc
#endif
#define malloc(size) FllocMalloc((size), __FILE__, __LINE__)

#ifdef calloc
#undef calloc
#endif
#define calloc(nmemb, size) FllocCalloc((nmemb), (size), __FILE__, __LINE__)

#ifdef realloc
#undef realloc
#endif
#define realloc(ptr, size) FllocRealloc((ptr), (size), __FILE__, __LINE__)

#ifdef free
#undef free
#endif
#define free(ptr) FllocFree(ptr, __FILE__, __LINE__)

#ifdef strdup
#undef strdup
#endif
#define strdup(s) FllocStrdup((s), __FILE__, __LINE__)

#ifdef strndup
#undef strndup
#endif
#define strndup(s, n) FllocStrndup((s), (n), __FILE__, __LINE__)

#endif /* !FLLOC_DISABLED */

#ifdef __cplusplus
}
#endif

#endif /* FLLOC_h_ */
