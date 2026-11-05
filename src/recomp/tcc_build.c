/*
 * tcc_build.c - compiles generated C in memory with the bundled TinyCC.
 *
 * Without HF_HAVE_LIBTCC (developer builds on Linux) it falls back to the
 * system's gcc and dlopen, which also gives an optimized build to compare.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tcc_build.h"

#ifdef HF_HAVE_LIBTCC
#include "libtcc.h"

static void on_error(void *opaque, const char *msg)
{
  char *err = (char *)opaque;
  if (!err[0]) snprintf(err, 256, "%s", msg);
}

void *tcc_build(const char *source, void **init_fn, char *err, size_t err_size)
{
  TCCState *s = tcc_new();
  char buf[256] = "";
  if (!s) { snprintf(err, err_size, "tcc_new failed"); return NULL; }
  tcc_set_error_func(s, buf, on_error);
  tcc_set_options(s, "-nostdlib -nostdinc -w");
  tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
  if (tcc_compile_string(s, source) < 0 || tcc_relocate(s) < 0)
  {
    snprintf(err, err_size, "compile failed: %s", buf);
    tcc_delete(s);
    return NULL;
  }
  *init_fn = tcc_get_symbol(s, "hf_native_init");
  if (!*init_fn) { snprintf(err, err_size, "no hf_native_init"); tcc_delete(s); return NULL; }
  return s;
}

void tcc_build_free(void *handle)
{
  if (handle) tcc_delete((TCCState *)handle);
}

#elif defined(__linux__)
#include <dlfcn.h>
#include <unistd.h>

void *tcc_build(const char *source, void **init_fn, char *err, size_t err_size)
{
  char c_path[256], so_path[256], cmd[700];
  const char *cc = getenv("HF_NATIVE_CC");
  FILE *f;
  void *h;
  snprintf(c_path, sizeof(c_path), "/tmp/hf_native_%d.c", (int)getpid());
  snprintf(so_path, sizeof(so_path), "/tmp/hf_native_%d.so", (int)getpid());
  f = fopen(c_path, "w");
  if (!f) { snprintf(err, err_size, "cannot write %s", c_path); return NULL; }
  fputs(source, f);
  fclose(f);
  snprintf(cmd, sizeof(cmd), "%s -w -shared -fPIC -fno-strict-aliasing -fwrapv %s -o %s",
           cc ? cc : "gcc -O1", c_path, so_path);
  if (system(cmd) != 0) { snprintf(err, err_size, "compiler failed"); return NULL; }
  h = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
  unlink(so_path);
  if (!getenv("HF_NATIVE_KEEP")) unlink(c_path);
  if (!h) { snprintf(err, err_size, "dlopen: %s", dlerror()); return NULL; }
  *init_fn = dlsym(h, "hf_native_init");
  if (!*init_fn) { dlclose(h); snprintf(err, err_size, "no hf_native_init"); return NULL; }
  return h;
}

void tcc_build_free(void *handle)
{
  if (handle) dlclose(handle);
}

#else
void *tcc_build(const char *source, void **init_fn, char *err, size_t err_size)
{
  (void)source; (void)init_fn;
  snprintf(err, err_size, "no compiler in this build");
  return NULL;
}
void tcc_build_free(void *handle) { (void)handle; }
#endif
