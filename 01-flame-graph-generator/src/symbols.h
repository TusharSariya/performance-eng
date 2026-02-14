/*
 * symbols.h — Symbol resolution for external process profiling
 *
 * Parses /proc/<pid>/maps and uses addr2line for address-to-symbol mapping.
 */
#ifndef SYMBOLS_H
#define SYMBOLS_H

#include <stdint.h>

/* Initialize the symbol resolver for a given PID.
 * Parses /proc/<pid>/maps to build the VMA table.
 * Returns 0 on success, -1 on failure. */
int sym_init(int pid);

/* Resolve a virtual address to a function name.
 * Returns a pointer to a static/cached string (do not free).
 * Returns "[unknown]" if resolution fails. */
const char *sym_resolve(uint64_t addr);

/* Free all resources used by the symbol resolver. */
void sym_cleanup(void);

#endif /* SYMBOLS_H */
