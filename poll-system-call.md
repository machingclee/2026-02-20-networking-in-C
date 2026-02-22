# Understanding the `poll()` System Call

## What is `poll()`?

`poll()` is a system call for I/O multiplexing that allows a program to monitor multiple file descriptors simultaneously, waiting for one or more to become ready for I/O operations. It's an alternative to `select()` that addresses some of its limitations.

## Why Use `poll()`?

**Problem:** A server needs to handle multiple clients, but traditional blocking I/O would freeze the entire server while waiting for data from one client.

**Solution:** `poll()` lets us monitor many file descriptors at once, blocking until at least one becomes ready. The kernel handles the waiting efficiently using interrupts, not busy-wait loops.

## The `poll()` Function Signature

```c
#include <poll.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout);
```

**Parameters:**
- `fds`: Array of `pollfd` structures describing file descriptors to monitor
- `nfds`: Number of file descriptors in the array
- `timeout`: How long to wait in milliseconds
  - `-1`: Block indefinitely until an event occurs
  - `0`: Return immediately (polling mode)
  - `> 0`: Wait up to this many milliseconds

**Return Value:**
- `> 0`: Number of file descriptors with events
- `0`: Timeout occurred, no events
- `-1`: Error occurred (check `errno`)

## The `pollfd` Structure

```c
struct pollfd {
    int fd;         // File descriptor to monitor
    short events;   // Events we want to monitor (input)
    short revents;  // Events that actually occurred (output)
};
```

**Key Fields:**
- `fd`: The file descriptor to watch (socket, file, pipe, etc.)
- `events`: Bitmask of events we're interested in (what we set)
- `revents`: Bitmask of events that occurred (what kernel sets)

## Event Flags (Bitmask Values)

**Common `events` flags we set:**
- `POLLIN` (0x0001): Data available to read
- `POLLOUT` (0x0004): Ready to write data
- `POLLPRI` (0x0002): Urgent data available

**Flags kernel may set in `revents`:**
- `POLLIN`: Data ready to read
- `POLLOUT`: Ready for writing
- `POLLERR` (0x0008): Error condition
- `POLLHUP` (0x0010): Hang up (connection closed)
- `POLLNVAL` (0x0020): Invalid file descriptor

## Checking Events with Bitwise Operations

When `poll()` returns, we check `revents` using bitwise AND (`&`) to see which events occurred:

```c
if (fds[i].revents & POLLIN) {
    // Data is ready to read
}

if (fds[i].revents & POLLOUT) {
    // Socket is ready for writing
}

if (fds[i].revents & POLLERR) {
    // An error occurred
}
```

**Why bitwise `&` instead of `&&`?**

Because `revents` is a bitmask where multiple events can be true simultaneously:
- `revents = 0x0009` means both `POLLIN` (0x0001) and `POLLERR` (0x0008) occurred
- `revents & POLLIN` checks if bit 0 is set
- `0x0009 & 0x0001 = 0x0001` (true, data ready despite error)

## How `poll()` Works Internally

### Not a Busy-Wait Loop

Despite its name, `poll()` does **not** continuously poll in a loop. Instead:

1. **Process goes to sleep:** Your process is put in a wait queue
2. **Kernel monitors hardware:** Network card generates interrupt when data arrives
3. **Interrupt wakes process:** Kernel's interrupt handler wakes your process
4. **Poll returns:** With events filled in `revents` fields

This is **event-driven** and highly efficient—our process uses zero CPU while waiting.

### The Kernel's Wait Queue Mechanism

```
[Our Process] --poll()--> [Kernel Wait Queue]
                                  |
                     [Monitoring file descriptors]
                                  |
              [Network card receives data]
                                  |
                   [Hardware triggers interrupt]
                                  |
              [Kernel interrupt handler runs]
                                  |
                [Marks fd as ready, sets revents]
                                  |
                [Wakes our process from queue]
                                  |
            [poll() returns with event count]
```

## Complete Server Example Walkthrough

Here's how a TCP server uses `poll()` to handle multiple clients:

### 1. Setup Socket with Options

```c
int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
int opt = 1;
setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

**`setsockopt()` explanation:**
- **Purpose:** Configure socket options
- **`SOL_SOCKET`:** Operate at socket level (not protocol-specific)
- **`SO_REUSEADDR`:** Allow immediate port reuse after restart
- **`opt = 1`:** Enable the option (0 would disable)

Without `SO_REUSEADDR`, restarting our server would fail with "Address already in use" for 30-120 seconds (TIME_WAIT period).

### 2. Initialize the pollfd Array

```c
#define MAX_CLIENTS 256
struct pollfd fds[MAX_CLIENTS + 1];
int nfds = 1;

// First entry is the listening socket
fds[0].fd = listen_fd;
fds[0].events = POLLIN;  // Watch for incoming connections
```

**Why `MAX_CLIENTS + 1`?**
- Index 0: The listening socket (accepts new connections)
- Index 1 to MAX_CLIENTS: Connected client sockets

### 3. Main Event Loop

```c
while (1) {
    // Rebuild the fds array with active client connections
    int ii = 1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientStates[i].fd != -1) {
            fds[ii].fd = clientStates[i].fd;
            fds[ii].events = POLLIN;
            ii++;
        }
    }

    // Wait for events (blocks here until something happens)
    int n_events = poll(fds, nfds, -1);
    
    if (n_events == -1) {
        perror("poll");
        exit(EXIT_FAILURE);
    }
```

**Key points:**
- We rebuild `fds` array each iteration with active clients
- `poll(fds, nfds, -1)` blocks indefinitely until events occur
- When it returns, `n_events` tells us how many fds have events

### 4. Handle New Connections

```c
if (fds[0].revents & POLLIN) {
    // Listening socket has an incoming connection
    int conn_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
    
    int freeSlot = find_free_slot();
    if (freeSlot == -1) {
        close(conn_fd);  // Server full
    } else {
        clientStates[freeSlot].fd = conn_fd;
        clientStates[freeSlot].state = STATE_CONNECTED;
        nfds++;
    }
    n_events--;  // One event processed
}
```

**Process:**
1. Check if listening socket has `POLLIN` event
2. `accept()` the new connection (returns new fd)
3. Find free slot in our client tracking array
4. Store the connection or reject if full
5. Decrement event counter

### 5. Handle Client Data

```c
for (int i = 1; i <= nfds && n_events > 0; i++) {
    if (fds[i].revents & POLLIN) {
        int fd = fds[i].fd;
        int slot = find_slot_by_fd(fd);
        
        ssize_t bytes_read = read(fd, &clientStates[slot].buffer, 
                                   sizeof(clientStates[slot].buffer));
        
        if (bytes_read <= 0) {
            // Connection closed or error
            close(fd);
            clientStates[slot].fd = -1;
            clientStates[slot].state = STATE_DISCONNECTED;
            nfds--;
        } else {
            printf("Received: %s\n", clientStates[slot].buffer);
        }
        n_events--;
    }
}
```

**Process:**
1. Loop through client sockets (index 1 onwards)
2. Check if each has `POLLIN` event (data ready)
3. `read()` the data from the socket
4. If `read()` returns ≤ 0, client disconnected
5. Clean up the slot and decrement `nfds`

## `poll()` vs `select()` Comparison

| Feature | `select()` | `poll()` |
|---------|-----------|----------|
| **Max FDs** | Limited to 1024 (FD_SETSIZE) | No fixed limit |
| **API** | Uses fd_set bitmask | Uses pollfd array |
| **Modification** | Modifies fd_set (must rebuild) | Separates events/revents |
| **Performance** | O(n) where n = highest fd | O(n) where n = actual count |
| **Clear API** | Less intuitive (FD_SET, FD_ISSET) | More straightforward |

**When to use `poll()`:**
- Need more than 1024 file descriptors
- Want cleaner, more maintainable code
- Don't need to modify the watched set frequently

**When to use `select()`:**
- Maximum portability (older systems)
- Very few file descriptors
- Timeout precision requirements

## Common Patterns and Best Practices

### 1. Always Check Return Value

```c
int n_events = poll(fds, nfds, -1);
if (n_events == -1) {
    perror("poll");
    // Handle error
}
```

### 2. Use Event Counter Optimization

```c
for (int i = 0; i < nfds && n_events > 0; i++) {
    if (fds[i].revents != 0) {
        // Process event
        n_events--;
    }
}
```

This lets us exit early once all events are processed instead of checking every fd.

### 3. Handle POLLHUP and POLLERR

```c
if (fds[i].revents & (POLLERR | POLLHUP)) {
    // Connection error or hangup
    close(fds[i].fd);
    // Clean up
}
```

### 4. Initialize Unused Slots

```c
memset(fds, 0, sizeof(fds));
// or
for (int i = 0; i < MAX_FDS; i++) {
    fds[i].fd = -1;  // -1 is ignored by poll()
}
```

Setting `fd = -1` tells `poll()` to ignore that array entry.

## Memory and Performance Considerations

**Advantages:**
- No fixed FD limit like `select()`
- Only loops through fds we actually registered
- Kernel efficiently uses interrupts, not busy-waiting
- Separates input (`events`) from output (`revents`)

**Disadvantages:**
- Still O(n) scan through all fds on each call
- Not as efficient as `epoll()` (Linux) or `kqueue()` (BSD) for thousands of connections
- Must rebuild array if fd set changes

## Modern Alternatives

For servers handling thousands of connections:
- **Linux:** `epoll()` - O(1) performance for ready fds
- **BSD/macOS:** `kqueue()` - Similar to epoll
- **Windows:** IOCP (I/O Completion Ports)
- **Cross-platform:** `libevent` or `libuv` libraries

But `poll()` is perfect for learning multiplexing concepts and handles hundreds of connections efficiently.

## Summary

- `poll()` monitors multiple file descriptors for I/O readiness
- Uses `pollfd` array with `fd`, `events`, and `revents` fields  
- Kernel puts process to sleep and wakes it via interrupts (not busy-wait)
- Check events using bitwise AND: `revents & POLLIN`
- Addresses `select()`'s limitations with cleaner API and no fd limit
- Ideal for servers with dozens to hundreds of concurrent connections
- Event-driven design enables efficient concurrent I/O handling
