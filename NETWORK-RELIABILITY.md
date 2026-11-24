# XSSH network reliability update (experimental)

This update preserves the RSA public-key blob fix and existing SSH login.

- The interactive channel already uses nonblocking libssh2 I/O. Its read loop remains bounded to 4 ms per frame, and pending keyboard bytes are retained across `LIBSSH2_ERROR_EAGAIN`.
- Enables TCP `SO_KEEPALIVE` where supported and configures SSH keepalive requests at 15-second idle intervals.
- Services libssh2 keepalives once per second from the connected terminal loop, treating `EAGAIN` as retryable.
- Shows a nonfatal status when queued outgoing input has made no progress for five seconds; it does not disconnect merely because the wireless extender is slow.

Limitations: initial TCP connect, SSH handshake, and authentication remain blocking, so slow networks can still delay the connection screen. TCP disconnects cannot resume the existing SSH session. Use tmux on the server for persistent work.

Validation: source-level checks only; an original Xbox/nxdk build and live wireless-extender test have not been performed here.
