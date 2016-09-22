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

#ifndef FLLOC_CRC32_h_
#define FLLOC_CRC32_h_


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>


/** Compute a CRC-32 checksum
 *
 * @param buffer [in] Data to compute checksum on; must not be NULL
 * @param size_B [in] Number of bytes in the above buffer
 *
 * @return The computed CRC-32 checksum
 */
uint32_t Crc32(const uint8_t* buffer, int size_B);


#endif /* FLLOC_CRC32_h_ */
