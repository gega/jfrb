# jfrb

## Jitter-Reducing Ring Buffer

Ring buffer utility designed for streaming processors that require predictable chunks of consecutive input data.

## API Reference

### `void jfrb_init(struct jfrb_s *rb, uint8_t *buf, uint32_t buflen, jfrb_read_t read, void *ud);`

Initializes a ring buffer instance.

* `rb` Pointer to a supplied `jfrb_s` structure.
* `buf` Backing storage for the ring buffer.
* `buflen` Size of the storage in bytes.
* `read` Callback used to read fresh data into the buffer.
* `ud` User-defined pointer passed to the `read` callback.

The buffer starts empty, with all internal state cleared.

---

### `uint8_t *jfrb_consume(struct jfrb_s *rb, uint32_t consumed);`

Returns a pointer to the next consecutive region of buffered data and advances the buffer position by `consumed` bytes.

`consumed` must be greater than zero and must not exceed the value returned by `jfrb_nx_size()`.

The returned pointer is valid for exactly `consumed` bytes.

---

### `int jfrb_nx_size(struct jfrb_s *rb);`

Returns the number of consecutive bytes currently available for consumption without wrapping.

If the current region is empty, this function may trigger a refill or roll the buffer window as needed. It guarantees that the returned size always corresponds to a fully continuous range.

A return value of zero indicates end-of-stream as reported by the `read` callback.

---

### `void jfrb_prefill(struct jfrb_s *rb);`

Attempts to refill the unused section(s) of the buffer without disturbing the currently valid data.

Useful for periodically aligning the buffer so that future `consume` calls get larger consecutive regions, which reduces jitter in downstream processing.

---

### `int jfrb_refill(struct jfrb_s *rb);`

Performs a full refill of the entire buffer using the `read` callback.

Resets `pos`, `top`, and `fill` so that the buffer becomes one continuous block of fresh data.

Returns zero on success.

---

# Typical Usage Pattern

```c
jfrb_init(&rb, buf, bufsiz, custom_read, NULL);     // initialize state
jfrb_refill(&rb);                                   // fill buffer before processing

left=total_bytes;

for(i=cnt=0;;i++,cnt++)
{
    int have = jfrb_nx_size(&rb);                   // available continuous bytes
    if (have == 0)                                  // EOF signaled by read callback
        break;

    int want = <next-layer-request-size>;
    int csm = MIN(want, have);
    csm = MIN(csm, left);

    uint8_t *p = jfrb_consume(&rb, csm);            // obtain data block
    process_data(p, csm);                           // process the chunk
    left -= csm;

    if (left <= 0)                                  // EOF from total limit
        break;

    if ((cnt % nth_fill) == 0)
        jfrb_prefill(&rb);                          // optional smoothing refill
}
```

This loop continuously requests the largest safe block of consecutive data, processes it, and occasionally performs a prefill to keep future reads predictable.
