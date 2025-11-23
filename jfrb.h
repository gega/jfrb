#ifndef JFRB_H
#define JFRB_H

#include <stdint.h>
#include <assert.h>

typedef int (*jfrb_read_t)(void *ud, uint8_t *b, uint32_t l);

struct jfrb_s
{
  jfrb_read_t read;
  void *ud;
  uint8_t *buf;
  uint32_t len;
  uint32_t pos;
  uint32_t top;
  uint32_t fill;
  uint32_t use;
};

void jfrb_init(struct jfrb_s *rb, uint8_t *buf, uint32_t buflen, jfrb_read_t read, void *ud);
uint8_t *jfrb_consume(struct jfrb_s *rb, uint32_t consumed);
int jfrb_nx_size(struct jfrb_s *rb);
void jfrb_prefill(struct jfrb_s *rb);
uint8_t *jfrb_next_chunk(struct jfrb_s *rb, int *size);

#ifdef JFRB_MACROS

#define jfrb_refill(rb) do{ (rb)->read((rb)->ud, (rb)->buf, (rb)->len); (rb)->fill=(rb)->pos=(rb)->use=0; (rb)->top=(rb)->len; }while(0)

#define jfrb_release_chunk(rb,s) do{ uint32_t _f=(s); if((s)<0||_f>(rb)->use) _f=(rb)->use; (rb)->pos+=_f; (rb)->use-=_f; }while(0)

#else
int jfrb_refill(struct jfrb_s *rb);
void jfrb_release_chunk(struct jfrb_s *rb, int size);
#endif

#endif

#ifdef JFRB_IMPLEMENTATION

void jfrb_init(struct jfrb_s *rb, uint8_t *buf, uint32_t buflen, jfrb_read_t read, void *ud)
{
  memset(rb, 0, sizeof(struct jfrb_s));
  rb->ud=ud;
  rb->read=read;
  rb->buf=buf;
  rb->len=buflen;
  rb->pos=0;
  rb->top=0;
  rb->use=0;
}

#ifndef JFRB_MACROS
void jfrb_release_chunk(struct jfrb_s *rb, int s)
{
  uint32_t f=s;

  if(s<0||f>rb->use) f=rb->use;
  rb->pos+=f;
  rb->use-=f;
}
#endif

/*
  the processor will consume "consumed" bytes from the buffer
  this is guaranteed to be larger than 0 and less or equal than
  jfrb_nx_size() and it is a consecutive buffer area

  this should roll over the buffer properly and if the data is
  not yet present, it should read one (1) chunk of data to continue
  the processing
  
 */
uint8_t *jfrb_consume_chunk(struct jfrb_s *rb, uint32_t consumed)
{
  uint8_t *r;

  if(consumed==0) return(NULL);
  r=&rb->buf[rb->pos];
  rb->use=consumed;
  return(r);
}

#ifndef JFRB_MACROS
int jfrb_refill(struct jfrb_s *rb)
{
  rb->read(rb->ud, rb->buf, rb->len);
  rb->fill=0;
  rb->top=rb->len;
  rb->pos=0;
  rb->use=0;
  return(0);
}
#endif

/*
   return the size of the buffer which is filled with data
   and consecutive (available for the next processing unit

              nx_size
            ____/\____
           /          \
   ........dddddddddddd.......
   ^buf    ^pos               ^buf+len

              nx_size
            ____/\____
           /          \
   22222222dddddddddddd.......
   ^buf    ^pos               ^buf+len

                 nx_size
            ________/\_______
           /                 \
   ddddddddddddddddddddddddddd
   ^buf    ^pos               ^buf+len

*/
int jfrb_prepare_chunk(struct jfrb_s *rb)
{
  int size=rb->top-rb->pos;
  rb->use=0;
  if(size==0)
  {
    if(rb->fill!=0)
    {
      size=rb->fill;
      rb->pos=0;
      rb->top=rb->fill;
      rb->fill=0;
    }
    else
    {
      jfrb_refill(rb);
      size=rb->top-rb->pos;
    }
  }
  return(size);
}


uint8_t *jfrb_next_chunk(struct jfrb_s *rb, int *size)
{
  return(jfrb_consume_chunk(rb,(*size=jfrb_prepare_chunk(rb))));
}

/*
  refills the partial buffer with fresh data so it can be 
  used in a circular fashion

  ........dddddddddddd.......
  ^buf    ^pos               ^buf+len
  
  22222222dddddddddddd1111111
  
  fills '1' using one read
  fills '2' with a second read call
  
*/
void jfrb_prefill(struct jfrb_s *rb)
{
  if(rb->pos>=rb->len)
  {
    // read from 'fill' to 'len'
    rb->read(rb->ud, &rb->buf[rb->fill], rb->len-rb->fill);
    rb->pos=0;
    rb->top=rb->len;
    rb->fill=0;
  }
  else
  {
    if(rb->top < rb->len)
    {
      // read from 'top' to 'len'
      rb->read(rb->ud, &rb->buf[rb->top], rb->len-rb->top);
      rb->top=rb->len;
    }
    if(rb->pos > rb->fill)
    {
      // read from 'fill' to 'pos'
      rb->read(rb->ud, &rb->buf[rb->fill], rb->pos-rb->fill);
      rb->fill=rb->pos;
    }
  }
}

#endif
