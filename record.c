#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <time.h>
#include <semaphore.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>

#include "file.h"

#define BUFFER_S (10)
#define INTERVAL (1000000LL)

typedef struct {
  jack_client_t *client;
  jack_port_t *port[2];
  uint32_t samplerate;
  jack_ringbuffer_t *rb;
  sem_t sem;
} rt_state_t;

typedef jack_default_audio_sample_t sample_t;

static inline void
writebuf(rt_state_t *state, void *data, unsigned size)
{
  if (jack_ringbuffer_write(state->rb, (char *)data, size) != size) {
    fprintf(stderr, "in rt: Ringbuffer full. Panic ensues\n");
    exit(1);
  }
}

int 
cb(jack_nframes_t nframes, void *arg)
{
  rt_state_t *state = (rt_state_t *)arg;
  struct timespec ts;
  uint64_t time;
  jack_nframes_t latency;
  sample_t *samples;
  int i;

  clock_gettime(CLOCK_REALTIME, &ts);
  time = ts.tv_sec*1000000LL + ts.tv_nsec/1000;
  
  latency = jack_port_get_total_latency(state->client, state->port[0]);
  time -= (latency*1000000LL)/state->samplerate;

  writebuf(state, &time, sizeof(time));
  writebuf(state, &nframes, sizeof(nframes));
  for(i = 0; i < 2; i++) {
    samples = (sample_t *)jack_port_get_buffer(state->port[i], nframes);
    writebuf(state, samples, sizeof(sample_t)*nframes);
  }
  sem_post(&state->sem);

  return 0;
}

void
mywrite(const void *buf, size_t sz)
{
  int ret;
  if (sz <= 0) return;
  ret = write(0, buf, sz);
  if (ret < 0) {
    perror("write");
    exit(1);
  }
  if (ret == 0) {
    fprintf(stderr, "nothing was written\n");
    exit(1);
  }
  mywrite(((char *)buf)+ret, sz-ret);
}

static file_t *file = NULL;
static int reload = 0;

void
mainloop(rt_state_t *state)
{
  uint64_t time;
  jack_nframes_t nframes, bufsize;
  sample_t *buf;
  int16_t *ibuf;
  unsigned i,j;
  
  bufsize = jack_get_buffer_size(state->client);

  buf = malloc(sizeof(sample_t)*bufsize);
  ibuf = malloc(sizeof(int16_t)*bufsize*2);
  
  for(;;) {
    while (jack_ringbuffer_read_space(state->rb) < (sizeof(uint64_t) + sizeof(jack_nframes_t))) {
      sem_wait(&state->sem);
    }
    jack_ringbuffer_read(state->rb, (char *)&time, sizeof(time));
    jack_ringbuffer_read(state->rb, (char *)&nframes, sizeof(nframes));
    
    assert(nframes <= bufsize);
    assert(jack_ringbuffer_read_space(state->rb) >= 2*nframes*sizeof(sample_t));
    
    for(j=0; j < 2; j++) {
      jack_ringbuffer_read(state->rb, (char*)buf, sizeof(sample_t)*nframes);
      for(i=0; i < nframes; i++) {
	ibuf[2*i+j] = buf[i]*(sample_t)INT16_MAX;
      }
    }
    
    if (file == NULL) {
      file = file_init(state->samplerate, INTERVAL, time);
    }

    file_write(file, time, ibuf, nframes);
    if (reload) {
      fprintf(stderr, "reloading\n");
      reload = 0;
      file_close(file);
      file = file_init(state->samplerate, INTERVAL, time);
      file_write(file, time, ibuf, nframes); //overlap
    }


  }
}
static rt_state_t *state_g;

static void handle_int(int sig)
{
  assert(sig == SIGINT);
  fprintf(stderr, "got SIGINT. terminating\n");
  jack_deactivate(state_g->client);
  jack_client_close(state_g->client);

  if (file != NULL) {
    file_close(file);
  }
  exit(0);
}

static void
handle_hup(int sig)
{
  assert(sig == SIGHUP);
  fprintf(stderr, "got SIGHUP\n");
  reload = 1;
}

static void
connect_ports(rt_state_t *state)
{
  const char **ports;
  int i;

  ports = jack_get_ports(state->client, 
			 NULL, NULL,
			 JackPortIsPhysical|JackPortIsOutput);
  if (ports == NULL) {
    fprintf(stderr, "No suitable ports found. Do the plumbing yourself\n");
    return;
  }
  for(i=0; i < 2; i++) {
    if (ports[i] == NULL) {
      fprintf(stderr, "Not enough capture-ports found\n");
      free(ports);
      return;
    }
    printf("connecting to %s\n", ports[i]);
    if (jack_connect(state->client, ports[i], jack_port_name(state->port[i]))) {
      fprintf(stderr, "Could not connect input-ports\n");
      free(ports);
      return;
    }
  }
  free(ports);
}

int
main(/*int argc, char **argv*/)
{
  rt_state_t *state = malloc(sizeof(rt_state_t));
  jack_status_t status;
  
  if (mlock(state, sizeof(rt_state_t)) != 0) {
    perror("mlock");
    exit(1);
  }
  if (sem_init(&state->sem, 0, 0) != 0) {
    perror("sem_init");
    exit(1);
  }

  state->client = jack_client_open("record", JackNoStartServer, &status);
  if (state->client == NULL) {
    fprintf(stderr, "Could not create jack client\n");
    exit(1);
  }
  state->samplerate = jack_get_sample_rate(state->client);

  state->port[0] = jack_port_register(state->client, "in-left", JACK_DEFAULT_AUDIO_TYPE,
				      JackPortIsInput, 0);
  if (state->port[0] == NULL) {
    fprintf(stderr, "Could not create left port\n");
    exit(1);
  }
  state->port[1] = jack_port_register(state->client, "in-right", JACK_DEFAULT_AUDIO_TYPE,
				      JackPortIsInput, 0);
  if (state->port[1] == NULL) {
    fprintf(stderr, "Could not create right port\n");
    exit(1);
  }
  if (jack_set_process_callback(state->client, cb, state) != 0) {
    fprintf(stderr, "Could not set callback\n");
    exit(1);
  }
  
  state->rb = jack_ringbuffer_create(2*state->samplerate*sizeof(sample_t)*BUFFER_S);
  if (state->rb == NULL) {
    fprintf(stderr, "could not allocate ringbuffer\n");
    exit(1);
  }
  if (jack_ringbuffer_mlock(state->rb) != 0) {
    fprintf(stderr, "Could not mlock ringbuffer.\n");
    exit(1);
  }

  state_g = state;
  if (signal(SIGINT, handle_int) != 0) {
    perror("signal");
    exit(1);
  }
  if (signal(SIGHUP, handle_hup) != 0) {
    perror("signal");
    exit(1);
  }
  
  if (jack_activate(state->client) != 0) {
    fprintf(stderr, "Could not activate jack\n");
    exit(1);
  }

  connect_ports(state);

  mainloop(state);
  return 0;
}
