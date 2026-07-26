# DynarmicTest12 technical notes

## Test11 result

Test11 fixed connection establishment and browser links. Its log confirmed:

- DNS resolution succeeded.
- TCP connected successfully.
- `SO_ERROR` translated as zero.
- HTTP request bytes were sent.
- Windows opened the Twitter URL successfully.

No response bytes reached the guest before legacy libcurl aborted with `Failure when receiving data from the peer`.

## Receive-path fix

The ARM libcurl uses nonblocking sockets. A Windows `recv()` can return `WSAEWOULDBLOCK` between request transmission and arrival of the first response byte. Test12 waits up to 15 seconds for readability with `select()`, verifies `SO_ERROR`, and retries the receive. It returns actual bytes, clean EOF, a real translated socket error, or EAGAIN after the bounded wait.

## Poll bridge

Test12 replaces `WSAPoll` with a direct translation between Android/Linux `pollfd` bits and Windows `select()` read/write/exception sets. This avoids Winsock poll-event differences and reports Linux-compatible `POLLIN`, `POLLOUT`, `POLLERR`, and `POLLNVAL` values.

## Safety

Diagnostics contain file names, socket state, byte counts, and errors only. HTTP request bodies, passwords, level data, and response bodies are not logged.
