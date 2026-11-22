/* Parameter-set dispatcher.
 *
 * Both keypairs are generated on the host by host/gen_key.c and kept side by
 * side; -DFALCON_BENCH_LOGN=10 selects Falcon-1024, anything else (or nothing)
 * selects Falcon-512. Every buffer size in the drivers derives from
 * EMBEDDED_KEY_LOGN, so no other file changes between parameter sets.
 */
#ifndef FALCON_KEY_DISPATCH_H
#define FALCON_KEY_DISPATCH_H

#if defined(FALCON_BENCH_LOGN) && FALCON_BENCH_LOGN == 10
#include "falcon_key_1024.h"
#else
#include "falcon_key_512.h"
#endif

#endif
