/*
 * tcc_build.c - compiles generated C in memory with the bundled TinyCC.
 *
 * On Linux, setting HF_NATIVE_CC (e.g. "gcc -O1") uses that compiler and
 * dlopen instead, to compare against an optimizing build. Builds without
 * TinyCC (not x86-64) report "no compiler" and the game stays emulated.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tcc_build.h"

#if defined(__linux__)
#include <dlfcn.h>
#include <unistd.h>
#define HAVE_SYSTEM_CC 1

static void *system_cc_build(const char *cc, const char *source, void **init_fn, char *err, size_t err_size)
{
  char c_path[256], so_path[256], cmd[700];
  FILE *f;
  void *h;
  snprintf(c_path, sizeof(c_path), "/tmp/hf_native_%d.c", (int)getpid());
  snprintf(so_path, sizeof(so_path), "/tmp/hf_native_%d.so", (int)getpid());
  f = fopen(c_path, "w");
  if (!f) { snprintf(err, err_size, "cannot write %s", c_path); return NULL; }
  fputs(source, f);
  fclose(f);
  snprintf(cmd, sizeof(cmd), "%s -w -shared -fPIC -fno-strict-aliasing -fwrapv %s -o %s",
           cc, c_path, so_path);
  if (system(cmd) != 0) { snprintf(err, err_size, "compiler failed"); return NULL; }
  h = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
  unlink(so_path);
  if (!getenv("HF_NATIVE_KEEP")) unlink(c_path);
  if (!h) { snprintf(err, err_size, "dlopen: %s", dlerror()); return NULL; }
  *init_fn = dlsym(h, "hf_native_init");
  if (!*init_fn) { dlclose(h); snprintf(err, err_size, "no hf_native_init"); return NULL; }
  return h;
}
#endif

#ifdef HF_HAVE_LIBTCC
#include "libtcc.h"

#ifdef _WIN32
/* TinyCC calls __chkstk for stack frames of 4 KB or more; on Windows it
   lives in TinyCC's runtime library, which we don't ship, so it is built
   along with the code (TinyCC's own win32/lib/chkstk.S, x86-64 part). */
static const char chkstk_src[] =
  "__asm__(\".globl __chkstk\\n"
  "__chkstk:\\n"
  " xchg (%rsp),%rbp\\n"
  " push %rbp\\n"
  " lea 8(%rsp),%rbp\\n"
  " push %rcx\\n"
  " mov %rbp,%rcx\\n"
  " movslq %eax,%rax\\n"
  "P0:\\n"
  " sub $4096,%rcx\\n"
  " test %rax,(%rcx)\\n"
  " sub $4096,%rax\\n"
  " cmp $4096,%rax\\n"
  " jge P0\\n"
  " sub %rax,%rcx\\n"
  " test %rax,(%rcx)\\n"
  " mov %rsp,%rax\\n"
  " mov %rcx,%rsp\\n"
  " mov (%rax),%rcx\\n"
  " jmp *8(%rax)\\n\");\n";
#endif

static void on_error(void *opaque, const char *msg)
{
  char *err = (char *)opaque;
  if (!err[0]) snprintf(err, 256, "%s", msg);
}

static void *tcc_mem_build(const char *source, void **init_fn, char *err, size_t err_size)
{
  TCCState *s = tcc_new();
  char buf[256] = "";
  if (!s) { snprintf(err, err_size, "tcc_new failed"); return NULL; }
  tcc_set_error_func(s, buf, on_error);
  tcc_set_options(s, "-nostdlib -nostdinc -w");
  tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
  /* TinyCC may call these for struct copies and zeroing */
  tcc_add_symbol(s, "memcpy", (const void *)memcpy);
  tcc_add_symbol(s, "memmove", (const void *)memmove);
  tcc_add_symbol(s, "memset", (const void *)memset);
  if (tcc_compile_string(s, source) < 0
#ifdef _WIN32
      || tcc_compile_string(s, chkstk_src) < 0
#endif
      || tcc_relocate(s) < 0)
  {
    snprintf(err, err_size, "compile failed: %s", buf);
    tcc_delete(s);
    return NULL;
  }
  *init_fn = tcc_get_symbol(s, "hf_native_init");
  if (!*init_fn) { snprintf(err, err_size, "no hf_native_init"); tcc_delete(s); return NULL; }
  return s;
}
#endif

/* handles carry which builder made them */
typedef struct { int kind; void *h; } Handle;
enum { BUILT_TCC = 1, BUILT_DL = 2 };

void *tcc_build(const char *source, void **init_fn, char *err, size_t err_size)
{
  Handle *hd = (Handle *)malloc(sizeof(Handle));
  if (!hd) { snprintf(err, err_size, "out of memory"); return NULL; }
  hd->h = NULL;
  hd->kind = 0;
#ifdef HAVE_SYSTEM_CC
  {
    const char *cc = getenv("HF_NATIVE_CC");
    if (cc && cc[0])
    {
      hd->h = system_cc_build(cc, source, init_fn, err, err_size);
      hd->kind = BUILT_DL;
    }
  }
#endif
#ifdef HF_HAVE_LIBTCC
  if (!hd->kind)
  {
    hd->h = tcc_mem_build(source, init_fn, err, err_size);
    hd->kind = BUILT_TCC;
  }
#endif
  if (!hd->kind)
  {
    (void)source; (void)init_fn;
    snprintf(err, err_size, "no compiler in this build");
  }
  if (!hd->h) { free(hd); return NULL; }
  return hd;
}

void tcc_build_free(void *handle)
{
  Handle *hd = (Handle *)handle;
  if (!hd) return;
#ifdef HF_HAVE_LIBTCC
  if (hd->kind == BUILT_TCC) tcc_delete((TCCState *)hd->h);
#endif
#ifdef HAVE_SYSTEM_CC
  if (hd->kind == BUILT_DL) dlclose(hd->h);
#endif
  free(hd);
}
