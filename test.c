#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define JFRB_MACROS
#define JFRB_IMPLEMENTATION
#include "jfrb.h"


#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif


struct testcase 
{
    int total_bytes;        // total number of bytes
    const int *consume;     // consumer chunk pattern
    int seed;               // reproducible test case
    int nth_fill;	    // extra fillup on Nth cosume
    size_t bufsiz;          // ring buffer size
};


static uint32_t crc32_table[256];

static void crc32_init(void)
{
  const uint32_t poly=0xEDB88320u;
  for(uint32_t i=0; i<256; i++)
  {
    uint32_t c=i;
    for(int j=0; j<8; j++) c=(c&1) ? (poly^(c>>1)) : (c>>1);
    crc32_table[i]=c;
  }
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t n)
{
  uint32_t c=~crc;
  while(n--) c=crc32_table[(c^*data++)&0xFFu]^(c>>8);
  return(~c);
}


static uint32_t rng_state;

static inline uint8_t fast_rand8(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint8_t)(rng_state >> 24);
}

static int fake_read(void *ud, uint8_t *dst, uint32_t max)
{
    uint8_t *p = dst;
    for (size_t i = 0; i < max; i++) p[i] = fast_rand8();
    return max;
}

static const int test1_consume[] = { 5, 6, 33, -1 };
static const int test2_consume[] = { 100, 1, 100, 1, 100, 1, -1 };
static const int test3_consume[] = { 256, -1 };
static const int test4_consume[] = { 1, 2, -1 };
static const int test5_consume[] = { 3, 7, 15, 2, 9, 128, 33, 64, -1 };
static const int test6_consume[] = { 512, 10, 1, 1, 1024, 33, 4, -1 };
static const int test7_consume[] = { 999, 1, 1, 500, 2, 2, 700, -1 };
static const int test8_consume[] = { 5, 4, -1 };

static const struct testcase tests[] = 
{
    { 262, test1_consume,  12345, 19, 256  },
    { 262, test1_consume,  12345, 18, 256  },
    { 1000001, test2_consume,  67890, 7, 512  },
    { 1024*2000, test3_consume,  1337,  11, 128  },
    { 1024*3072, test4_consume,  42,    9, 64   },
    { 1024*100, test5_consume,  2025,  4, 300  },
    { 544, test6_consume,  99,    5, 1024 },
    { 1024*22, test7_consume,  54321, 8, 200  },
    { 1111, test8_consume,  777,   3, 1110  },
    {0,NULL}
};

uint32_t reference_crc(const struct testcase *tc)
{
  uint32_t crc=0;
  uint8_t data;

  rng_state=tc->seed;
  crc32_init();
  for(int i=0; i<tc->total_bytes; i++)
  {
    data=fast_rand8();
    crc=crc32_update(crc, &data, 1);
  }
  return(crc);
}

uint8_t *glob_data;

uint32_t run_test(const struct testcase *tc)
{
  struct jfrb_s rb;
  uint8_t *buf;
  uint32_t crc=0;
  int cnt,left;
  uint8_t *d;

  rng_state=tc->seed;
  buf=malloc(tc->bufsiz);
  if(glob_data) free(glob_data);
  d=glob_data=malloc(tc->total_bytes);
  crc32_init();

  jfrb_init(&rb, buf, tc->bufsiz, fake_read, NULL);	// init
  jfrb_refill(&rb);					// full buffer refill

  int have=0;
  left=tc->total_bytes;
  uint8_t *p;
#if 1
  // Example of the compact combined API
  while((p=jfrb_next_chunk(&rb, &have)) && left>0)	// compact api gives size and pointer
  {
    // process_data(p, MIN(have, left));
    int csm=MIN(have, left);				// if read cb cannot detect EOF it could be too large
    memcpy(d, p, csm);
    d+=csm;
    crc=crc32_update(crc, p, csm);
    jfrb_release_chunk(&rb, csm);                       // release the buffer for refilling
    memset(p, 0, csm);
    left-=csm;
    if((cnt++%tc->nth_fill)==0) jfrb_prefill(&rb);	// occasional prefill to reduce jitter (frame boundary)
  }
#else
  // Usage of the separated API for finer grain control
  for(int i=cnt=0;;i++,cnt++)
  {
    if(tc->consume[i]<=0) i=0;
    int want=tc->consume[i];
    have=jfrb_prepare_chunk(&rb);			// continuous data
    if(have==0) break; 					// EOF (detected by ringbuffer)
    int csm=MIN(want, have);
    csm=MIN(csm, left);
    p=jfrb_consume_chunk(&rb, csm);			// get data, size must be <= jfrb_nx_size()
    memcpy(d,p,csm);
    d+=csm;
    crc=crc32_update(crc, p, csm);
    jfrb_release_chunk(&rb, csm);                       // release the buffer for refilling
    memset(p,0x55,csm);
    left-=csm;
    if(left<=0) break;					// EOF (detected by loop)
    if((cnt%tc->nth_fill) == 0) jfrb_prefill(&rb);	// occasional prefill to reduce jitter (frame boundary)
  }
#endif

  free(buf);

  return(crc);
}

void testcase(const struct testcase *tc)
{
  uint32_t ref_crc=reference_crc(tc);
  uint32_t ring_crc=run_test(tc);

  if(ref_crc!=ring_crc)
  {
    fprintf(stderr,"FAIL: seed=%d bufsiz=%zu total_bytes=%d Nth-prefill=%d\n", tc->seed, tc->bufsiz, tc->total_bytes, tc->nth_fill);
    rng_state = tc->seed;
    uint8_t *d=glob_data;
    int i;
    char left[3*8+1];
    char right[3*8+1];
    char *l=left;
    char *r=right;
    int print=0;
    for(i=0;i<tc->total_bytes;i++)
    {
      uint8_t ref=fast_rand8();
      l+=sprintf(l," %02x",ref);
      r+=sprintf(r," %02x",d[i]);
      if(ref!=d[i]) print++;
      if(((i+1)%8)==0 || (i+1)==tc->total_bytes)
      {
        if(print>0&&print<16) fprintf(stderr,"%06x  %s  | %s\n",i-7,left,right);
        left[0]=right[0]='\0';
        l=left;
        r=right;
      }
    }
    fprintf(stderr,"   EXTRA: %02x\n",fast_rand8());
  }
  else 
  {
    fprintf(stderr,"PASS: data=%8d seed=%6d bufsiz=%6zu crc=%08X\n", tc->total_bytes, tc->seed, tc->bufsiz, ring_crc);
  }
}

int main(void)
{
  for(int i=0;tests[i].consume!=NULL;i++) testcase(&tests[i]);
  if(glob_data) free(glob_data);
  return(0);
}
