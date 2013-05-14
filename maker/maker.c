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

#include <magic.h>

#include "../config.h"
#include "../common/common.h"

int main(int argc, char **argv)
{
  int arch = 32;
  magic_t magic;

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
  if (stat(VST_BRIDGE_TPL_PATH, &st_tpl)) {
    fprintf(stderr, "%s: %m\n", VST_BRIDGE_TPL_PATH);
    return 1;
  }

  magic = magic_open(MAGIC_NONE);
  if (!magic)
    fprintf(stderr, "failed to initialize magic\n");
  else {
    magic_load(magic, NULL);
    if (strstr(magic_file(magic, argv[1]), "80386"))
      arch = 32;
    else if (strstr(magic_file(magic, argv[1]), "x86-64"))
      arch = 64;
    printf("detected %d bits dll\n", arch);
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

  void *dll_path = memmem(mem_so, st_tpl.st_size, VST_BRIDGE_TPL_MAGIC,
                          sizeof (VST_BRIDGE_TPL_MAGIC));
  if (!dll_path) {
    fprintf(stderr, "template magic not found in plugin\n");
    return 1;
  }
  strncpy(dll_path, argv[1], PATH_MAX);

  void *host_path = memmem(mem_so, st_tpl.st_size, VST_BRIDGE_HOST32_PATH,
                           sizeof (VST_BRIDGE_HOST32_PATH));
  if (!host_path) {
    fprintf(stderr, "host path not found in plugin\n");
    return 1;
  }

  if (arch == 32)
    memcpy(host_path, VST_BRIDGE_HOST32_PATH, sizeof (VST_BRIDGE_HOST32_PATH));
  else
    memcpy(host_path, VST_BRIDGE_HOST64_PATH, sizeof (VST_BRIDGE_HOST64_PATH));
  munmap(mem_so, st_tpl.st_size);

  close(fd_so);

  return 0;
}
