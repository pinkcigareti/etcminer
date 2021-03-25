/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#define FNV_PRIME 0x01000193

#define fnv(x, y) ((x)*FNV_PRIME ^ (y))

DEV_INLINE uint4 fnv4(uint4 a, uint4 b) {
    uint4 c;
    c.x = a.x * FNV_PRIME ^ b.x;
    c.y = a.y * FNV_PRIME ^ b.y;
    c.z = a.z * FNV_PRIME ^ b.z;
    c.w = a.w * FNV_PRIME ^ b.w;
    return c;
}

DEV_INLINE uint32_t fnv_reduce(uint4 v) { return fnv(fnv(fnv(v.x, v.y), v.z), v.w); }
