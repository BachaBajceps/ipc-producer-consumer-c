# IPC Producer-Consumer Pipeline in C

A self-contained **three-stage producer–consumer chain** showcasing classic UNIX
inter-process communication:

* **Shared memory** (`shmget` / `shmat`) – Producer → Transformer  
* **System V message queues** (`msgget` / `msgsnd` / `msgrcv`) – Transformer → Consumer  
* **Counting semaphores** (`semget` / `semop`) – back-pressure between stages  
* **Asynchronous control via POSIX signals** propagated through an auxiliary queue

Originally built as a university Operating Systems assignment; now open-sourced
for anyone who needs a working, non-trivial IPC example.

## Repository layout

```text
.
├── producer_consumer.c       # ← core source (rename if you prefer main.c)
├── cleanup-ipc.sh            # helper for wiping stray IPC objects
├── README.md                 # this file
├── LICENSE                   # MIT license text
```

## Build

```bash
gcc -std=c11 -Wall -pedantic -o ipc-pipeline producer_consumer.c
```


## Usage <a id="examples"></a>

```bash
# 1) Interactive keyboard input
./ipc-pipeline

# 2) Process any text or binary file
./ipc-pipeline < path/to/file

# 3) Stress-test with random data
./ipc-pipeline < /dev/urandom | head -c 1M > /dev/null
```

The *parent* spawns the three workers and exits immediately.  
Send signals to **any** worker `pid`; the intent is relayed to its siblings.

| Action               | Signal   | Example                         |
|----------------------|----------|---------------------------------|
| Graceful shutdown    | SIGUSR2  | `kill -USR2  <pid>`             |
| Pause processing     | SIGINT   | `kill -INT   <pid>`             |
| Resume processing    | SIGCONT  | `kill -CONT  <pid>`             |

All workers exchange the information via a message queue and raise
`SIGUSR1`, keeping the pipeline in sync.




## Cleaning leftover IPC objects

Some crash scenarios can leave System V IPC objects behind.  
Run **as root**:

```bash
sudo ./cleanup-ipc.sh
```

The script deletes any dangling shared-memory segments, message queues or
semaphores and force-kills stray `./ipc-pipeline` processes.

