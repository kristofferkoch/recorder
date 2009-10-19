#ifndef __FILE_H
#define __FILE_H
#include <stdint.h>
#include <unistd.h>

#define FILE_MAGIC (6893732045233840104)

typedef struct {
  /* config: */
  uint32_t rate;
  uint64_t interval;

  /*init*/
  int fd, lame;
  uint64_t start;
  pid_t pid;

  /* state: */
  uint64_t nframes;
  uint64_t last_stamp;
  uint16_t frames_since_stamp;
} file_t;

file_t *file_init(uint32_t rate, uint64_t interval, uint64_t time);
void file_write(file_t *file, uint64_t time, const int16_t *frames, unsigned nframes);
void file_close(file_t *file);
#endif
