#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/time.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <poll.h>

#include <inttypes.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>

#include <sys/ioctl.h>
#include <string.h>

#include <linux/fs.h>

#include "revbits.h"

#include "delta_config.h"

#include "delta.h"

#include "delta_binary.h"

/* One block to be mined. */
typedef struct blockspec {
  long blockno;      // number of the 4KiB block in the table
  uint64_t here;     // delta-encoding offset of the block
  uint64_t target;   // endpoint we want to find
  int tbl;           // table id
  char * bl_memaddr; // address where the block is
  int j;             // fragment position in burst
} blockspec;

pthread_barrier_t barrier;
pthread_mutex_t dmutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dcond = PTHREAD_COND_INITIALIZER;

/* How many tables, colors and keystream samples do we have in one burst */
#define tables 40
#define colors 8
#define samples 51

int max(int a, int b) {
  if (a>b) {
    return a;
  } else {
    return b;
  }
}

volatile uint64_t * fragments;

#define devices (sizeof(devpaths) / sizeof(devpaths[0]))

char * storages[devices];

/* mmap table devices */
void mmap_devices() {

  for(int i = 0; i<devices; i++) {
    size_t dsize;
    int fd = open(devpaths[i], O_RDONLY|O_DIRECT);

    ioctl(fd, BLKGETSIZE64, &dsize);

    printf("mmap %s %li bytes fd %i ", devpaths[i], dsize, fd);

    storages[i] = (char*)mmap(NULL, dsize, PROT_READ, MAP_PRIVATE, fd, 0);

    printf("result %p\n", storages[i]);

  }

}

/* Block metadata */
blockspec blockq[tables*colors*samples];
int blockqptr = 0;

/* Save block metadata and advise the kernel to cache the block for us.

  blockno: number of the 4KiB block in the table
  here:    delta-encoding offset of the block
  target:  endpoint we want to find
  tbl:     table id
  j:       fragment position in burst
 */
int mined;
void MineABlockNCQ(long blockno, uint64_t here, uint64_t target, int tbl, int j, FILE* fp) {

  // printf("Searching for endpoint, block %lx, blockstart %lx, endpoint %lX, table %i, idx %i\n", blockno, here, target, tbl, j);

  blockno += offsets[tbl];

  // compute block address in memory
  int64_t a = blockno*4096;
  //char * maddr = storages[devs[tbl]] + a;

  char * scratch = malloc(4096);
  //memcpy(scratch, maddr, 4096);
  fseek(fp, a, SEEK_SET);
  fread(scratch, 4096, 1, fp);

  uint64_t re = CompleteEndpointSearch(scratch, here, target);
  if(re) {
    re=rev(ApplyIndexFunc(re, 34));
    mined++; // must be mutex-protected
  }

  fragments[j] = re;
  free(scratch);
}

int numt = 40;

#define GIGA 1000000000L

typedef struct tr {
  int tid;
  pthread_t thr;
  int start;
  int stop;
  int bp;
} tr;

tr * t;

void * mujthread(void *);

/* Init the machine. This is to be called once on the library load. */
void delta_init() {
  //mmap_devices();
  load_idx();

  if(!t) {
    t = calloc(numt, sizeof(tr));
  }

  //printf("Erecting barrier\n");
  //pthread_barrier_init(&barrier, NULL, numt);

  printf("Creating threads\n");

  for(int i = 0; i<numt; i++) {
    //pthread_join(t[i].thr, NULL);
    pthread_t thr;
    t[i].tid = i;

    t[i].start = i*408;
    t[i].stop = (i+1)*408;

    int ret = pthread_create(&thr, NULL, &mujthread, (void*) (t + i));
    t[i].thr = thr;

    if(ret != 0) {
      printf("Cannot create thread!\n");
      exit(EXIT_FAILURE);
    }
  }


}

int jobs = 0;

int qrun = 0;

void * mujthread(void *ptr) {
  tr * ctx = (tr*)ptr;
  //printf("Thread %i, range %i %i, is waiting on barrier\n", ctx->tid, ctx->start, ctx->stop);

  //pthread_barrier_wait(&barrier);

  int mytbl = -1;

  FILE *ptr_myfile;

  int qpb = 0;

  while(1) {
    //printf("Thread %i, range %i %i, waiting on barrier\n", ctx->tid, ctx->start, ctx->stop);
    //pthread_barrier_wait(&barrier);
    //printf("Thread %i, range %i %i, passed the barrier\n", ctx->tid, ctx->start, ctx->stop);

    while(qrun == qpb) { // hope we have atomic 4B assignments, otherwise this will terribly fail
      poll(NULL, 0, 1); // XXX I don't know how to use barriers without this
    }

    qpb = qrun;

    ctx->bp = qpb;

    for(int tbl=0; tbl<tables; tbl++) {
      for(int i=0; i<(colors*samples); i++) {
        int j = i+(tbl*colors*samples);
        if(j >= ctx->start && j < ctx->stop) {
          if(mytbl >= 0 && mytbl != tbl) {
            printf("I/O scatter violation! (expected %i got %i at %i)\n", mytbl, tbl, j);
            exit(1);
          }
          if(mytbl == -1) { // first run, open file
            ptr_myfile=fopen(devpaths[devs[tbl]],"rb");
			if(!ptr_myfile) {
				fprintf(stderr, "error: failed to open %s (%s)\n", devpaths[devs[tbl]], strerror(errno));
				exit(EXIT_FAILURE);
			}
            mytbl = tbl;
          }

          uint64_t tg = fragments[j];
          tg = rev(tg);
          StartEndpointSearch(tg, tbl, j, ptr_myfile);

        }
      }
    }
    pthread_mutex_lock(&dmutex);
    jobs += ctx->stop - ctx->start;
    pthread_mutex_unlock(&dmutex);
  }

  fclose(ptr_myfile);

  printf("Thread %i done\n", ctx->tid);

  return(NULL);
}


/* Prepare block for all fragments from burst in cbuf. */

void ncq_submit(char * cbuf, int size) {

  /*FILE *ptr_myfile;
  ptr_myfile=fopen("burst.bin","wb");
  fwrite(cbuf, size, 1, ptr_myfile);
  fclose(ptr_myfile);*/

  return;
}

/* Wrapper for MineBlocksMmap */
void ncq_read(char * cbuf, int size) {
  struct timespec start, end;

  jobs = 0;

  clock_gettime(CLOCK_REALTIME, &start);
  fragments = (uint64_t *)cbuf;

  qrun++;
  //printf("Destroying barrier\n");
  //pthread_barrier_destroy(&barrier);

  int be = 0;
  while(1) {
    /*if(be == 0) {
      int allr = 1;
      for(int i = 0; i<numt; i++) {
        if(t[i].bp != qrun) {
          allr = 0;
        }
      }
      if(allr == 1) {
        printf("Erecting barrier\n");
        pthread_barrier_init(&barrier, NULL, numt);
        be = 1;
      }
    }*/
    pthread_mutex_lock(&dmutex);
    //printf("jobs: %i\n", jobs);
    if(jobs>=16320) {
      break;
    }
    pthread_mutex_unlock(&dmutex);
    poll(NULL, 0, 1);
  }

  pthread_mutex_unlock(&dmutex);

  clock_gettime(CLOCK_REALTIME, &end);
  long long unsigned int diff = GIGA*(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
  printf("Delta lookup: %llu ms (%i)\n", diff/1000000, mined);

  return;

}


volatile uint64_t ble;
int main() {
  delta_init();

  uint64_t * scb = (uint64_t*)malloc(8*16320);

  FILE *ptr_myfile;
  ptr_myfile=fopen("burst.bin","rb");
  fread(scb, 130560, 1, ptr_myfile);
  fclose(ptr_myfile);

  /*for(int i = 0; i<16320; i++) {
    scb[i] = random();
  }*/

  printf("init done, run!\n");

  ncq_read((char*)scb, 8*16320);

  /*for(int i = 0; i<16320; i++) {
    ble = scb[i];
    printf("%x", ble&0x1);
  }*/

  return 0;

}


