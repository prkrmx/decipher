#include <stdio.h>
#include <sys/time.h>
#include <assert.h>
#include <stdlib.h>

#include <inttypes.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>

#include <sys/ioctl.h>
#include <string.h>

#include <linux/fs.h>

#include <limits.h>

#include "tables.h"

#include "revbits.h"

#include "vankusconf.h"

/*Added by Max for broadcast messages*/ 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#define h_addr h_addr_list[0] /* for backward compatibility */



/* The idea is that work is loaded by burst_load() into burstq[] and then burstq[] is periodically submitted to the GPU via 
   frag_clblob(). The GPU returns some work solved to report() each time. The report() function sees what is solved and sets some flags in 
   burstq[] accordingly. Once some burst in burstq[] is finishet, it is kicked out. "Free slots" is how many free places are in burstq[]. */

/* Struct defining one fragment. */
typedef struct __attribute__((__packed__)) fragment_s {
  uint64_t prng;
  uint64_t job;
  uint64_t pos;
  uint64_t iters;
  uint64_t table;
  uint64_t color;
  uint64_t start;
  uint64_t stop;
  uint64_t challenge;
} fragment;

/* Struct defining coordinates of one fragment
   (burst number + position in burst). */
typedef struct fragdbe {
  int burst;
  int pos;
} fragdbe;

// fragments in one burst
#define BURSTFRAGS 16320
// size of one fragment in GPGPU buffer (prng + rf + challenge + flags)
#define ONEFRAG 4
// size of buffer exchanged with oclvankus.py
#define BMSIZE (BURSTFRAGS*sizeof(fragment))

// queue of bursts that we are cracking
fragment ** burstq[QSIZE];
// helper structure to keep track of fragments we are sending to GPGPU
fragdbe fragdb[CLBLOBSIZE];
// and pointer to its top
int fragdbptr = 0;

/* stack of computed solutions (really, the strings like "Found xxx"
   or "crack #xxx took xxx") and pointer to its top */
#define SOLSIZE (100)
char solutions[20][SOLSIZE];
int solptr = 0;


/*Added by Max for broadcast messages*/ 
int sock, length;
struct sockaddr_in server;  // IP Addressing(IP, port, type) Stuff
struct hostent *hp;     		// DNS stuff
char buffer[SOLSIZE];
int on = 1;

int sock1, length1;
struct sockaddr_in server1;  // IP Addressing(IP, port, type) Stuff
struct hostent *hp1;     		// DNS stuff
int on1 = 1;

int initFlag = 0;

/* Get free position in the burst queue */
int getfirstfree() {
  int i;
  for(i=0; i<QSIZE; i++) {
    if(burstq[i] == NULL) {
      return i;
    }
  }
  return -1;
}

/* Get number of free slots in the burst queue - if there are free slots,
   our master will ask for more work. */
int getnumfree() {
  int fr = 0;
  for(int i=0; i<QSIZE; i++) {
    if(burstq[i] == NULL) {
      fr++;
    }
  }
  return fr;
}

/* Add one new burst from cbuf to queue. */
int burst_load(char * cbuf, int size) {

  int idx = getfirstfree();

  if(idx == -1) {
    printf("Attempted to push burst to full queue!\n");
    return -1;
  }

  burstq[idx] = (fragment**) malloc(BMSIZE);

  memcpy(burstq[idx], cbuf, BMSIZE);

  return 0;

}

/* Get value of the reduction function for a given table and color. */
uint64_t getrf(uint64_t table, uint64_t color) {
  //printf("t %li c %li\n", table, color);
  return rft[table + offset + color];
}

/* Return 1 if the fragment is finished. */
int fincond(fragment f) {
  if(f.color >= f.stop) {
    return 1;
  }
  if(f.prng == 0) {
    return 1;
  }
  return 0;
}

/* helper function for qsort */
int cmpint (const void * a, const void * b) {
   return ( *(int*)a - *(int*)b );
}

/* Get index of the i-th oldest burst. We want to work as FIFO, so we
   prioritize the oldest ones. Some people say the cracker should work as
   LIFO, which could be changed by, say, inverting the cmpint function. */
int getprio(int idx) {

  int jobnums[QSIZE];

  /* QSIZE is about 30, so we do not care about time complexity. */

  for(int i=0; i< QSIZE; i++) {
    if(burstq[i] == NULL) {
      jobnums[i] = INT_MAX;
      continue;
    }
    fragment* arr = (fragment*) burstq[i];
    jobnums[i] = arr[i].job;
  }

  qsort(jobnums, QSIZE, sizeof(int), cmpint);

  for(int i=0; i < QSIZE; i++) {
    if(burstq[i] != NULL) {
      fragment* arr = (fragment*) burstq[i];
      if(jobnums[idx] == arr[0].job) {
        return i;
      }
    }
  }

  return -1;

}

/* Generate GPGPU buffer. Write it to cbuf and return number of fragments it
   contains. */
int frag_clblob(char * cbuf, int size) {

  fragdbptr = 0;

  uint64_t * clblob = (uint64_t *) cbuf;

  for(int i = 0; i<QSIZE; i++) {

    int bp = getprio(i);

    if(bp < 0) { // no more bursts available
      break;
    }

    if(burstq[bp] != NULL) {
      fragment* arr = (fragment*) burstq[bp];

      for(int j = 0; j<BURSTFRAGS; j++) {

        if(fragdbptr >= CLBLOBSIZE) { // buffer full
          return fragdbptr;
        }

        fragment f = arr[j];

        if(fincond(f) == 0) {

          fragdbe e;
          e.burst = bp;
          e.pos = j;
          fragdb[fragdbptr] = e;

          clblob[ONEFRAG * fragdbptr + 0] = f.prng;
          clblob[ONEFRAG * fragdbptr + 1] = getrf(f.table, f.color);
          clblob[ONEFRAG * fragdbptr + 2] = f.challenge;

          fragdbptr++;

        } else {
          //printf("fragment %i:%i %lx %lx %lx %lx %lx\n", bp, j, f.prng, f.job, f.pos, f.table, f.color);
        }
      }
    }
  }

  return fragdbptr;
}

/* Parse the returned GPGPU buffer (cbuf). Update fragments in burstq with
   new, recomputed ones. And yield the "Found" message if the key was found. */
void report(char * cbuf, int size) {

  uint64_t * a = (uint64_t *)cbuf;

  //Broadcast stuff
  ////////////////////////////////////////////////////////////////////////
	if(!initFlag)
	{
		sock = socket(AF_INET, SOCK_DGRAM, 0);
		setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
		server.sin_family = AF_INET;
		hp = gethostbyname("10.255.255.255");

		bcopy((char*)hp->h_addr, (char*)&server.sin_addr, hp->h_length);
		server.sin_port = htons(13101);
		length = sizeof(struct sockaddr_in);


		sock1 = socket(AF_INET, SOCK_DGRAM, 0);
		setsockopt(sock1, SOL_SOCKET, SO_BROADCAST, &on1, sizeof(on1));
		server1.sin_family = AF_INET;
		hp1 = gethostbyname("10.255.255.255");

		bcopy((char*)hp1->h_addr, (char*)&server1.sin_addr, hp1->h_length);
		server1.sin_port = htons(13102);
		length1 = sizeof(struct sockaddr_in);


		initFlag = 1;
	}
  ////////////////////////////////////////////////////////////////////////

  for(int i = 0; i<fragdbptr; i++) {

    fragdbe e = fragdb[i];

    fragment* arr = (fragment*) burstq[e.burst];

    /* The kernel could return distinguished point, but the flag is not set. Fix it. */
    uint64_t x = a[i * ONEFRAG] ^ getrf(arr[e.pos].table, arr[e.pos].color);
    if((x & 0xFFF0000000000000ULL) == 0) {
      //assert(getrf(arr[e.pos].table, arr[e.pos].color) == a[i * ONEFRAG + 1]);
      //printf("Happened, %" PRIx64 " %" PRIx64 "\n", a[i * ONEFRAG], a[i * ONEFRAG+1]);
      a[i * ONEFRAG] = x;
      a[i * ONEFRAG + 3] |= 1;
    }

    /*if(e.pos == 1) {
      printf("Returned: %" PRIx64 ", flags %" PRIx64 ", color %" PRIx64 "\n", a[i * ONEFRAG], a[i * ONEFRAG+3], getrf(arr[e.pos].table, arr[e.pos].color));
      fflush(stdout);
    }*/

    if (a[i * ONEFRAG + 3] & 0x1) { // end of color

      /* if there is no next RF, no pre-reversing is used */
      if(arr[e.pos].color < arr[e.pos].stop - 1) {
        arr[e.pos].prng = a[i * ONEFRAG + 0] ^ getrf(arr[e.pos].table, arr[e.pos].color + 1);
      } else {
        arr[e.pos].prng = a[i * ONEFRAG + 0];
      }
      arr[e.pos].iters = 0;
      arr[e.pos].color++;

    } else { // resubmit

      arr[e.pos].prng = a[i * ONEFRAG + 0];
      arr[e.pos].iters++;

    }

    if (a[i * ONEFRAG + 3] & 0x2ULL) { // key found
      uint64_t state = rev(a[i * ONEFRAG + 0]);

      memset(solutions[solptr], 0, SOLSIZE);

      snprintf(solutions[solptr], SOLSIZE, "Found %016lX @ %li  #%li  (table:%li)", state, arr[e.pos].pos, arr[e.pos].job, arr[e.pos].table);
			
			// Send Broadcast message 
			snprintf(buffer, SOLSIZE, "FND %li %016lX %li", arr[e.pos].job, state, arr[e.pos].pos);      
      sendto(sock, buffer, strlen(buffer), 0, &server, length);
      sendto(sock1, buffer, strlen(buffer), 0, &server1, length1);

      //printf("to solq: %s\n", solutions[solptr]);

      printf("FOUND %lX fdbpos %i\n",state, i);

      solptr++;
    }

  }

  fragdbptr = 0;

}

/* Scan burstq for computed bursts and return them. Or return the "took" message
   if the finished burst was the challenge lookup.

   Return the computed burst in cbuf and return the job number.
 */
int pop_result(char * cbuf, int size) {

  //Broadcast stuff
  ////////////////////////////////////////////////////////////////////////
	if(!initFlag)
	{
		sock = socket(AF_INET, SOCK_DGRAM, 0);
		setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
		server.sin_family = AF_INET;
		hp = gethostbyname("10.255.255.255");

		bcopy((char*)hp->h_addr, (char*)&server.sin_addr, hp->h_length);
		server.sin_port = htons(13101);
		length = sizeof(struct sockaddr_in);


		sock1 = socket(AF_INET, SOCK_DGRAM, 0);
		setsockopt(sock1, SOL_SOCKET, SO_BROADCAST, &on1, sizeof(on1));
		server1.sin_family = AF_INET;
		hp1 = gethostbyname("10.255.255.255");

		bcopy((char*)hp1->h_addr, (char*)&server1.sin_addr, hp1->h_length);
		server1.sin_port = htons(13102);
		length1 = sizeof(struct sockaddr_in);


		initFlag = 1;
	}
  ////////////////////////////////////////////////////////////////////////

  uint64_t * a = (uint64_t *)cbuf;

  //printf("pop result %p %p %p\n", burstq[0], burstq[1], burstq[2]);

  //printf("Missing");
  for(int i = 0; i < QSIZE; i++) {

    // how many fragments in that bursts are *not* finished
    int missing = 0;
    // and how many are challenge lookup
    int chall = 0;


    if(burstq[i] != 0) {

      fragment* arr = (fragment*) burstq[i];

      for(int j = 0; j < BURSTFRAGS; j++) {
        fragment f = arr[j];
        if((fincond(f) == 0)) { // not finished
          //printf("No cond for %i:%i %lx %lx %lx %lx %lx\n", i, j, f.prng, f.job, f.pos, f.table, f.color);
          missing++;
        }
        if(f.challenge != 0) { // challenge lookup
          chall++;
        }
      }
      //printf(" %i", missing);
      if(missing == 0) { // yay, the burst is finished

        fragment* arr = (fragment*) burstq[i];

        /* extract prng values to build raw burst structure */
        for(int b = 0; b < BURSTFRAGS; b++) {
          fragment f = arr[b];
          a[b] = f.prng;
        }

        int jobnum = arr[0].job; // for historical reasons, there is a jobnum in every fragment

        /* finished && challenge lookup -> job is finished */
        if(chall > 0) {
          snprintf(solutions[solptr], SOLSIZE, "crack #%i took", jobnum);
					// Send Broadcast message 
					snprintf(buffer, SOLSIZE, "CRK %i", jobnum);
				  	sendto(sock, buffer, strlen(buffer), 0, &server, length);
				 	sendto(sock1, buffer, strlen(buffer), 0, &server1, length1);

          solptr++;
        }

        free(burstq[i]);
        burstq[i] = 0;
        if(chall > 0) {
          return -1;
        }
        return jobnum;
      }
    }
  }
  //printf("\n");

  return -1;

}

/* Pop solution from solutions stack. Return the solution in cbuf or return
   -1 if this was a pop from an empty stack. */
int pop_solution(char * cbuf, int size) {

  if (solptr > 0) {
    solptr--;
    //printf("sol: %s .. %s\n", solutions[0], solutions[1]);
    memcpy(cbuf, solutions[solptr], SOLSIZE);
    return 0;
  } else {
    return -1;
  }

}


