# TinyCC, as bundled here

Upstream: https://github.com/TinyCC/tinycc, commit
32d1c0a5614b9979768b3711726db3d601750ed3 (0.9.28rc). LGPL 2.1, see `COPYING`.

Only the files needed to build libtcc for x86-64 (Linux ELF and Windows PE)
are copied, unchanged. `config.h` is ours, and `tccdefs_.h` was made from
`include/tccdefs.h` with TinyCC's own `c2str` (`conftest.c -DC2STR`).

hellfire-saga-pc uses it to compile, in memory, the C that its recompiler
writes from the player's ROM. It is built as a separate library
(`libtcc` in CMakeLists.txt) and can be swapped for any other libtcc build.
