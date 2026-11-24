# v11.6.31 experimental keyboard latency update

- Flush SDL keyboard input immediately after draining SDL events.
- Flush USB keyboard and controller input immediately after polling them.
- Preserve nonblocking libssh2 write queue, partial-write handling and EAGAIN retries.
- Track EAGAIN occurrences, partial writes and queue-to-first-write delay.
- On a five-second stall, display queued duration and EAGAIN/partial counters.
- Keep the existing read burst, TCP_NODELAY, keepalives and RSA fixes unchanged.

These counters measure local queue time, **not** end-to-end server echo latency.
A temporary stall may still be caused by network congestion or SSH protocol flow control.
No Xbox toolchain is available in this environment; compile and device testing are pending.
