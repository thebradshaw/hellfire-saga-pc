/*
 * hf_file.h - fopen that accepts UTF-8 paths on every platform.
 * (On Windows, plain fopen fails for paths with non-English characters.)
 */
#ifndef HF_FILE_H
#define HF_FILE_H

#include <stdio.h>

FILE *hf_fopen(const char *utf8_path, const char *mode);

#endif
