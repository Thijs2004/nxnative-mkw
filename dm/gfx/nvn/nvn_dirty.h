// Write-protect dirty-page tracking for guest memory (nvn_dirty.c)
#pragma once
#include <stddef.h>
#include <stdint.h>

void nvn_dirty_add_range(const void *mem, size_t size);           // Register a memory pool as a tracked range (the first call registers the page-fault hook and the host pre-write hook)
void nvn_dirty_stat(int *live, int *peak, int *cap);                // number of registered memory pools, peak, tracking table capacity (for the memory readout line)
void nvn_dirty_remove_range(const void *mem);                     // Pool destruction: restore protected pages to writable and mark invalid
int nvn_dirty_tracked(const void *addr, size_t len);
void nvn_dirty_protect(const void *addr, size_t len);              // Set the covered pages read-only
uint64_t nvn_dirty_now(void);                                      // Current generation: take the content's stamp before nvn_dirty_protect and before reading it
int nvn_dirty_since(const void *addr, size_t len, uint64_t stamp); // Covered pages were written after the stamp -> 1
const void *nvn_dirty_span(const void *addr, size_t len);          // The tracked range holding the span (a handle to keep; NULL = untracked)
int nvn_dirty_span_covers(const void *span, const void *addr, size_t len);   // whether a kept handle still names a live range holding the span
int nvn_dirty_since_span(const void *span, const void *addr, size_t len, uint64_t stamp);
int nvn_dirty_since_keep(const void **span, const void *addr, size_t len, uint64_t stamp);   // the same, keeping in *span the range found (a walk over many runs of one pool scans the ranges once)
int nvn_dirty_range_since(const void *span, uint64_t stamp);     // a kept range had any page written after the stamp (or is no longer live) -> 1   // nvn_dirty_since through a kept handle (rechecked, scanned again only if it no longer holds the span)
void nvn_dirty_stats(uint64_t *faults, uint64_t *preps);           // Cumulative: number of page faults triggered by game writes, number of pages unprotected before host writes
void nvn_dirty_mp_stats(uint64_t *calls, uint64_t *pages, uint64_t *ns);   // Cumulative: this module's mprotect call count, pages covered, time spent
void nvn_dirty_slot_stats(int *live, int *used_slots, uint64_t *adds_total); // Currently live slots, slot table high-water mark, total number of registrations
size_t nvn_dirty_release(const void *addr, size_t len, const void *expect);   // give back the memory of the protected whole pages of the span whose bytes are expect (the host keeps them): bytes given
void nvn_dirty_released(uint64_t *given, uint64_t *in_file);   // cumulative: bytes given back, bytes asked for in the guest memory file (not given)
size_t nvn_dirty_drop(const void *addr, size_t len);   // drop the protected whole pages of the span (bytes nobody reads again; punched out of the guest memory file): bytes dropped
uint64_t nvn_dirty_dropped(void);                      // cumulative bytes dropped
