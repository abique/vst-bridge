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

#include "../config.h"
#include "../common/common.h"

# define TPL_PATH INSTALL_PREFIX "/lib/vst-bridge/vst-bridge-plugin-tpl.so"

int main(int argc, char **argv)
{
  if (argc != 3) {
    fprintf(stderr, "usage: %s <vst.dll> <vst.so>\n", argv[0]);
    return 2;
  }

  struct stat st_dll;
  if (stat(argv[1], &st_dll)) {
    fprintf(stderr, "%s: %m\n", argv[1]);
    return 1;
  }

  struct stat st_tpl;
  if (stat(TPL_PATH, &st_tpl)) {
    fprintf(stderr, "%s: %m\n", TPL_PATH);
    return 1;
  }

  // copy file
  int fd_tpl = open(TPL_PATH, O_RDONLY, 0644);
  if (fd_tpl < 0) {
    fprintf(stderr, "%s: %m\n", TPL_PATH);
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
    fprintf(stderr, "copy %s to %s: %m\n", TPL_PATH, argv[2]);
    return 1;
  }

  close(fd_tpl);

  void *mem_so = mmap(NULL, st_tpl.st_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                      fd_so, 0);
  if (mem_so == MAP_FAILED) {
    fprintf(stderr, "mmap(%s): %m\n", argv[2]);
    return 1;
  }

  void *dll_path = memmem(mem_so, st_tpl.st_size, VST_BRIDGE_TPL_PLUGIN_PATH,
                          sizeof (VST_BRIDGE_TPL_PLUGIN_PATH));
  if (!dll_path) {
    fprintf(stderr, "template plugin path not found\n");
    return 1;
  }

  strncpy(dll_path, argv[1], PATH_MAX);
  munmap(mem_so, st_tpl.st_size);
  close(fd_so);

  return 0;
}
