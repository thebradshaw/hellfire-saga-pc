/*
 * tcc_build.h - compiles generated C in memory with the bundled TinyCC.
 */
#ifndef HF_TCC_BUILD_H
#define HF_TCC_BUILD_H

#include <stddef.h>

/* Compiles source and looks up hf_native_init. Returns a handle for
   tcc_build_free, or NULL with a message in err. */
void *tcc_build(const char *source, void **init_fn, char *err, size_t err_size);
void  tcc_build_free(void *handle);

#endif
