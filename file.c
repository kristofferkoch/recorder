#include "file.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>

#define SECOND (1000000LL)

static void
mywrite(int fd, const void *buf, size_t sz)
{
  ssize_t ret;
  if (sz == 0) return;
  //fprintf(stderr, "writing %d bytes\n", sz);
  ret = write(fd, buf, sz);
  if (ret < 0) {
    perror("write");
    exit(1);
  }
  if (ret == 0) {
    fprintf(stderr, "No data written\n");
    exit(1);
  }
  mywrite(fd, ((char *)buf)+ret, sz-ret);
}

static void
start_lame(file_t *file)
{
  char ratestr[128], mp3file[128];
  int fd[2];
  pid_t cpid;
  snprintf(ratestr, 127, "%d", (unsigned)file->rate);
  snprintf(mp3file, 127, "%016llx.mp3", (unsigned long long)file->start);
  if (pipe(fd) != 0) {
    perror("pipe");
    exit(1);
  }
  cpid = fork();
  if (cpid == -1) {
    perror("fork");
    exit(1);
  }
  if (cpid == 0) {
    // Child code
    if (close(fd[1]) == -1) {
      perror("close(fd[1])");
      _exit(1);
    }
    if (dup2(fd[0], 0) == -1) {
      perror("dup2");
      _exit(1);
    }
    execlp("lame", "lame", "-r", "-s", ratestr, "--bitwidth", "16",
	   "--signed", "--little-endian", "--preset", "standard",
	   "-", mp3file, NULL);
    perror("execlp");
    _exit(1);
  } else {
    if (close(fd[0]) == -1) {
      perror("close(fd[0])");
      exit(1);
    }
    file->pid = cpid;
    file->lame=fd[1];
  }
}

void
file_close(file_t *file)
{
  close(file->fd);
  close(file->lame);
  waitpid(file->pid, NULL, 0);
  free(file);
}

file_t *
file_init(uint32_t rate, uint64_t interval, uint64_t time)
{
  file_t *ret = malloc(sizeof(file_t));
  char filename[256];
  int fd;

  ret->rate = rate;
  ret->interval = interval;
  ret->nframes = 0;
  ret->start = time;
  ret->last_stamp = time;
  ret->frames_since_stamp = 0;
 
  start_lame(ret);
  
  snprintf(filename, 255, "%016llx.stamps", (unsigned long long int)time);
  
  fd = open(filename, O_WRONLY|O_CREAT|O_EXCL, 00644);
  if (fd < 0) {
    perror("open");
    exit(1);
  }
  {
    uint64_t magic = FILE_MAGIC;
    mywrite(fd, &magic, sizeof(magic));
  }
  mywrite(fd, &rate, sizeof(rate));
  mywrite(fd, &interval, sizeof(interval));
  mywrite(fd, &time, sizeof(time));
  
  ret->fd = fd;
  
  return ret;
}
void
file_write(file_t *file, uint64_t time, const int16_t *frames, unsigned nframes)
{
  int64_t time_missing;
  uint64_t now, frames_missing;
  uint64_t u64;
  assert(file != NULL);
  mywrite(file->lame, frames, nframes*2*sizeof(int16_t));
  
  now = file->last_stamp + file->interval;
  if (time + (nframes*SECOND)/file->rate >= now) {
    time_missing = now - time;
    assert(time_missing >= 0);
    // simple interpolation
    frames_missing = (time_missing*file->rate)/SECOND;
    assert(frames_missing <= nframes);
    //printf("Time missing: %lld µs\tframes missing: %llu\n", time_missing, frames_missing);

    u64 = file->nframes + frames_missing;
    mywrite(file->fd, &u64, sizeof(u64));
    {
      uint64_t interval = file->frames_since_stamp + frames_missing;
      int64_t delta = interval - (file->rate*file->interval)/SECOND;

      printf("%llu\t This interval: %llu frames (delta: %lld)\t Average rate: %f\n", 
	     (unsigned long long int)u64,
	     (unsigned long long int)interval,
	     (long long int)delta,
	     ((double)(file->nframes)*SECOND)/(double)(time-file->start));
    }
    file->last_stamp = now;
    file->frames_since_stamp = nframes-frames_missing;
  } else {
    file->frames_since_stamp += nframes;
  }
  file->nframes += nframes;
}
