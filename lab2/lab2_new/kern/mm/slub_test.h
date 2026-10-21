#ifndef __KERN_MM_SLUB_TEST_H__
#define __KERN_MM_SLUB_TEST_H__

#include <defs.h>

#ifdef SLUB_TEST
void slub_check(void);

/* Test-build-only seam, not an object allocation API. The layout query is
 * read-only. Failure injection rejects precisely the next SLUB page request,
 * before entering the real PMM; pending proves that the request reached it.
 */
size_t slub_test_header_bytes(void);
void slub_test_fail_next_page_alloc(void);
bool slub_test_failure_pending(void);
#endif

#endif /* !__KERN_MM_SLUB_TEST_H__ */
