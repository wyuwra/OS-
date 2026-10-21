#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>

/* Call once after pmm_init(), with the kernel direct map available.
 * Access must be serialized, as for the underlying Lab2 page allocator.
 * This implementation targets the current single-hart boot environment.
 */
void slub_init(void);

/* Positive sizes: >= size writable bytes, aligned to at least 16 bytes.
 * Zero, overflow, or an unsatisfied request returns NULL without side effects.
 * Contents are unspecified. Call only after slub_init().
 */
void *slub_alloc(size_t size);

/* NULL is a no-op. Otherwise ptr must be a live allocation's starting address.
 * Empty slabs and large allocations return their pages immediately.
 * Invalid/interior pointers and double frees are outside the contract.
 */
void slub_free(void *ptr);

#endif /* !__KERN_MM_SLUB_H__ */
