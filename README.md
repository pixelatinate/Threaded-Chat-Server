# Threaded Chat Server

A multi-room chat server in C using pthreads, written for COSC 360 (Systems Programming) at the University of Tennessee, Lab 9. Users connect with `nc` or telnet, pick a name and a room, and chat with everyone else in that room.

The lab spec is in [`index.html`](index.html). The server is in [`chat_server.c`](chat_server.c), which is annotated throughout.

## How it works

- **One thread per room.** Each room has a guest list, a message queue, a mutex and a condition variable. The room thread sleeps on the condition variable until a message is queued, then delivers it to everyone on the guest list.
- **One thread per client.** Each client thread reads lines from its user. To post a join, a message or a leave, it locks the room, appends to the room's lists, signals the condition variable, and unlocks.
- **One lock per room**, and no thread ever holds two, so lock ordering can't deadlock.
- **The room directory has no lock.** `main` builds every room and starts its thread before it opens the port, so the directory is read-only by the time any client thread exists.

Data structures come from James Plank's libfdr: a red-black tree (`JRB`) for the room directory and doubly-linked lists (`Dllist`) for guest lists and message queues. A copy is in `local-build/libfdr`.

## Build

The top-level `makefile` points at paths on the UTK lab machines. To build anywhere else (tested on macOS with clang):

```bash
cd local-build && make
```

## Run and demo

Start the server with a port and one or more room names:

```bash
./local-build/chat_server 8005 Football Bridge Politics
```

Then connect from two or more other terminals:

```bash
nc localhost 8005
```

Each user sees the room list and who's in each room, enters a name, then enters a room. Messages show up for everyone in the same room. Press Ctrl-D to leave.

## Changes since the original submission

The original version passed all of the course's gradescripts. In November 2026 I went back through it, annotated it, and fixed some small issues:

- Leaving a room now finds the user by pointer instead of by name, so two users with the same name can't remove each other.
- Newlines are stripped with `strcspn`, so Windows-style `\r\n` input works and empty input can't write before the buffer.
- An unknown room name now disconnects only that user. Before, it shut down the whole server.
- Removed a memory leak, an unused struct field, dead code and a typo in an `fdopen` mode.

## Known issues

- **Double `fclose`.** `fin` and `fout` share one descriptor, so the second `fclose` fails. If another thread accepts a connection in between, it can get the same descriptor number and the second `fclose` closes that new connection. The fix is to `dup()` the descriptor for one of the streams.
- **Slow clients stall a room.** The room thread holds the lock while it writes to every socket, so one client that stops reading blocks everyone in that room. Each client could have its own outgoing queue instead.
- **The "who's online" listing reads guest lists without taking each room's lock.**
- **Message buffers are built with `strcpy`/`strcat`.** A long name plus a long message can overflow; `snprintf` would bound it.
- **`SIGPIPE` isn't ignored**, so writing to a client that just disconnected can end the process.
- **The `pthread_cond_wait` isn't in a `while` loop**, which is the standard guard against spurious wakeups.
- **The same three-line cleanup is repeated about 20 times** and would be one `drop_client()` helper.
- **Thread-per-client doesn't scale** to thousands of connections. An event loop (`poll`/`epoll`/`kqueue`) would.
