#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <magic.h>

#include "../config.h"
#include "../common/common.h"

void replace_magic(void *mem_so, size_t mem_sz,
                   const char *magic, const char *replacement);

int main(int argc, char **argv)
{
  int arch = 32;
  magic_t magic;
  char dll_real_path[PATH_MAX];
  char wineprefix_real_path[PATH_MAX];

  if (argc != 3 && argc != 4) {
    fprintf(stderr, "usage: %s <vst.dll> <vst.so> [<wine-prefix>]\n", argv[0]);
    return 2;
  }

  int has_wineprefix = argc == 4;

  struct stat st_dll;
  if (stat(argv[1], &st_dll) ||
      !realpath(argv[1], dll_real_path)) {
    fprintf(stderr, "%s: %m\n", argv[1]);
    return 1;
  }

  struct stat st_tpl;
  if (stat(VST_BRIDGE_TPL_PATH, &st_tpl)) {
    fprintf(stderr, "%s: %m\n", VST_BRIDGE_TPL_PATH);
    return 1;
  }

  if (has_wineprefix) {
    struct stat st_wineprefix;

    if (stat(argv[3], &st_wineprefix) ||
        !realpath(argv[3], wineprefix_real_path)) {
      fprintf(stderr, "%s: %m\n", argv[3]);
      return 1;
    }

    if (!S_ISDIR(st_wineprefix.st_mode)) {
      fprintf(stderr, "%s: %s\n", argv[3], strerror(ENOTDIR));
      return 1;
    }
  }

  magic = magic_open(MAGIC_NONE);
  if (!magic)
    fprintf(stderr, "failed to initialize magic\n");
  else {
    magic_load(magic, NULL);
    if (strstr(magic_file(magic, dll_real_path), "80386"))
      arch = 32;
    else if (strstr(magic_file(magic, dll_real_path), "x86-64"))
      arch = 64;
    printf("%s: detected %d bits dll\n", dll_real_path, arch);
    magic_close(magic);
  }

  // copy file
  int fd_tpl = open(VST_BRIDGE_TPL_PATH, O_RDONLY, 0644);
  if (fd_tpl < 0) {
    fprintf(stderr, "%s: %m\n", VST_BRIDGE_TPL_PATH);
    return 1;
  }

  int fd_so = open(argv[2], O_CREAT | O_TRUNC | O_RDWR, 0755);
  if (fd_so < 0) {
    fprintf(stderr, "%s: %m\n", argv[2]);
    return 1;
  }

  if (fchmod(fd_so, 0755))
    fprintf(stderr, "chmod(%s, 0755): %m\n", argv[2]);

  if (sendfile(fd_so, fd_tpl, NULL, st_tpl.st_size) != st_tpl.st_size) {
    fprintf(stderr, "copy %s to %s: %m\n", VST_BRIDGE_TPL_PATH, argv[2]);
    return 1;
  }

  close(fd_tpl);

  void *mem_so = mmap(NULL, st_tpl.st_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                      fd_so, 0);
  if (mem_so == MAP_FAILED) {
    fprintf(stderr, "mmap(%s): %m\n", argv[2]);
    return 1;
  }

  replace_magic(mem_so, st_tpl.st_size, VST_BRIDGE_TPL_DLL, dll_real_path);
  replace_magic(mem_so, st_tpl.st_size, VST_BRIDGE_TPL_HOST, arch == 32 ? VST_BRIDGE_HOST32_PATH : VST_BRIDGE_HOST64_PATH);
  if (has_wineprefix)
    replace_magic(mem_so, st_tpl.st_size, VST_BRIDGE_TPL_WINEPREFIX, wineprefix_real_path);

  munmap(mem_so, st_tpl.st_size);

  close(fd_so);

  return 0;
}

void replace_magic(void *mem_so, size_t mem_sz,
                   const char *magic, const char *replacement)
{
  char pattern[PATH_MAX];

  // Just to make sure we replace only values of PATH_MAX length.
  memset(pattern, 0, sizeof(pattern));
  strcpy(pattern, magic);

  void *pos = memmem(mem_so, mem_sz, pattern, sizeof(pattern));
  if (!pos) {
    fprintf(stderr, "`%s' magic not found in plugin\n", magic);
    exit(1);
  }

  // Remove all the rubbish. Probably will never matter.
  memset(pattern, 0, sizeof(pattern));
  strcpy(pattern, replacement);

  memcpy(pos, pattern, sizeof(pattern));
}
