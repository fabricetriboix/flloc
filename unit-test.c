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

#include "flloc.h"
#include <stdlib.h>
#include <stdio.h>

#define COUNT 100000
static unsigned char* gPointers[COUNT];
static int gSizes[COUNT];

int main()
{
    int i;
    for (i = 0; i < COUNT; i++) {
        gSizes[i] = 10 + (2 * i);
        gPointers[i] = malloc(gSizes[i]);
    }

    // Corrupt a couple of memory blocks
    int fault1 = COUNT / 3;
    int fault2 = (2 * COUNT) / 3;
    gPointers[fault1][-4] = 0xff;
    gPointers[fault2][gSizes[fault2] + 2] = 0x00;
    const char* filename = "expected-corruptions.txt";
    FILE* f = fopen(filename, "w");
    if (NULL == f) {
        fprintf(stderr, "Failed to create file '%s'\n", filename);
        exit(1);
    }
    fprintf(f, "%p\n", &(gPointers[fault1][-4]));
    fprintf(f, "%p\n", &(gPointers[fault2][gSizes[fault2] + 2]));
    fclose(f);

    // Do not free a couple of memory blocks, including one we just corrupted
    filename = "expected-leaks.txt";
    f = fopen(filename, "w");
    if (NULL == f) {
        fprintf(stderr, "Failed to create file '%s'\n", filename);
        exit(1);
    }
    for (i = 0; i < COUNT; i++) {
        if ((i != (fault1 + 1)) && (i != fault2)) {
            free(gPointers[i]);
        } else {
            fprintf(f, "%p\n", gPointers[i]);
        }
    }
    fclose(f);

    FllocCheck();
    return 0;
}
