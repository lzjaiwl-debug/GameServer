#ifndef LEPTONICA_ENDIANNESS_H
#define LEPTONICA_ENDIANNESS_H

#if defined(_WIN32) || defined(_WIN64)
#define L_LITTLE_ENDIAN 1
#define L_BIG_ENDIAN 0
#elif defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define L_LITTLE_ENDIAN 1
#define L_BIG_ENDIAN 0
#else
#define L_LITTLE_ENDIAN 0
#define L_BIG_ENDIAN 1
#endif
#else
#define L_LITTLE_ENDIAN 1
#define L_BIG_ENDIAN 0
#endif

#endif
