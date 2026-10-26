# Bounded Producer–Consumer Queue and Thread Pool (C++17)

A small, dependency-free C++17 concurrency project using only the standard library: a thread-safe bounded blocking queue with graceful shutdown, and a fixed-size thread pool built on top of it.

I built it to learn multithreaded C++ properly, by reproducing concurrency bugs on purpose, measuring them, and fixing them. The "Challenges" section below records what went wrong and how I fixed it.

## Highlights

- **`Queue<T>`:** a generic, bounded, blocking queue using one mutex and two condition variables, with a non-blocking `try_pop` and a `close()` that lets consumers drain remaining items before exiting.
- **`ThreadPool`:** N worker threads pulling `std::function<void()>` tasks from the queue, with RAII shutdown (close, drain, join).
- **Measured results:** CPU time dropped from about 1 to 2 s to 0.002 s by replacing polling with condition variables, and a 10-task workload dropped from 1.0 s to 0.1 s going from 1 to 10 workers.
- **Verified:** the thread pool demo runs clean under ThreadSanitizer.

**Concepts demonstrated:** mutexes and lock scope, condition variables and predicates, lost/missed wakeups, backpressure, RAII (`lock_guard`, `unique_lock`, thread joining in destructors), class templates, const-correctness (`mutable`), lambda captures, and reading template compiler errors.

## Files

| File | What it is |
|---|---|
| `queue.hpp` | `Queue<T>`: bounded blocking queue (header-only, because it is a template) |
| `thread_pool.hpp` | `ThreadPool`, built on `Queue` |
| `main.cpp` | Thread pool demo |
| `producer_consumer.cpp` | One producer and one consumer sharing a capacity-2 queue, showing backpressure and `close()` shutdown |

## Build and run

```bash
# thread pool demo
g++ -std=c++17 -pthread -Wall -Wextra main.cpp -o main
./main

# producer/consumer demo
g++ -std=c++17 -pthread -Wall -Wextra producer_consumer.cpp -o producer_consumer
./producer_consumer
```

Check for data races with ThreadSanitizer:

```bash
g++ -std=c++17 -pthread -g -fsanitize=thread main.cpp -o main.out
./main.out
```

## Design

**`Queue<T>`**

- One `std::mutex` protects all shared state: the queue and the `is_closed_` flag. Every read and write goes under the same mutex.
- Two condition variables: `not_empty_` (consumers wait on it) and `not_full_` (producers wait on it). Separate cvs mean a notification only reaches threads that can use it.
- `push` blocks while the queue is full (backpressure). `pop` blocks while the queue is empty.
- Waits use a predicate, so spurious wakeups and "stolen" wakeups are handled by re-checking the condition.
- `try_pop` is the non-blocking variant. It returns `false` for "empty" whether or not the queue is closed.
- `close()` sets the flag under the lock and calls `notify_all()` on both cvs. Consumers **drain** the remaining items, and `pop` returns `false` only when the queue is empty **and** closed. `push` returns `false` after close.
- `size()` is `const`, so the mutex is `mutable`.
- The constructor is `explicit` and rejects capacity 0.

**`ThreadPool`**

- Workers run `while (tasks_.pop(task)) task();`.
- Tasks run **outside** the queue's lock. If a worker ran `task()` while holding the queue's mutex, every other worker and every `submit` would be blocked for the duration of the task, and the threads would act like one. In this `Queue`, `pop` has already returned and released the lock before `task()` runs.
- The destructor calls `close()` and then joins every worker (RAII). Closing first matters: joining first would hang, because the workers would sleep forever on an empty queue.
- The pool is non-copyable, since it owns threads and a mutex-holding queue.

## Results

**Polling vs condition variable** (measured on an earlier version of the demo: 1 producer pushing every 500 ms, 1 consumer, 5 items, `time ./main`; the polling consumer looped on `try_pop`):

| Consumer | `user` CPU time |
|---|---|
| Busy-waiting on `try_pop` | about 1 to 2 s |
| Blocking `pop` with a condition variable | **0.002 s** |

The consumer now sleeps instead of spinning.

**Backpressure** (`producer_consumer.cpp`: capacity 2, producer with no sleep, consumer sleeping 300 ms per item): the producer races ahead until the queue is full, then each `added` follows a `took`. The consumer's 300 ms sleep sets the pace, and the producer is throttled to match. The run takes about 3 s for 10 items, and all 10 arrive in order. The log lines are printed outside the queue's lock, so they can appear slightly out of step with the real queue state.

**Thread pool speedup** (10 tasks of 100 ms each):

| Workers | Real time |
|---|---|
| 1 | 1.005 s |
| 3 | 0.405 s |
| 10 | 0.106 s |

The time follows `ceil(tasks / workers)` rounds of 100 ms, so the speedup is real.

**ThreadSanitizer:** the thread pool demo runs with no reports.

## Challenges and how I solved them

I deliberately reproduced each bug before fixing it, so I could see the failure rather than just read about it.

| Problem | What I saw | Root cause | Fix |
|---|---|---|---|
| **Lost updates** | Two threads incrementing a shared counter 100,000 times each gave 100,005 to 164,024 instead of 200,000, and the result changed on every run | `counter++` is three steps (load, add, store), and threads interleave between them | Protected the counter with a mutex (`lock_guard`) and got 200,000 every time |
| **Lock scope** | Locking once around the whole loop ran in 0.005 s versus 0.017 s for locking per increment | Holding the lock for the whole loop removes the lock/unlock cost but also removes all parallelism | Lock only the shared-data access and keep slow work outside the lock. This became the rule for the thread pool, where tasks run outside the queue's lock |
| **Segfault from an unlocked `push`** | Four threads pushing 1,000 ints each crashed once I removed the lock | `std::queue` wraps a `std::deque`, which keeps a table of block pointers. Concurrent pushes can both allocate blocks or free the table while another thread writes through it (use-after-free) | Every access to shared state, reads and writes, goes under the same mutex |
| **Check-then-act race in `try_pop`** | Two consumers could both see "not empty", then both read the same front item or pop an empty queue (undefined behavior) | Check, read and pop are separate steps | Do all three under one lock so they happen as one indivisible unit |
| **Busy-waiting** | A consumer polling `try_pop` used 1 to 2 s of CPU while the producer slept, and it fought the producer for the mutex | Polling burns CPU and contends for the lock | Added condition variables so the consumer sleeps until notified. CPU dropped to 0.002 s |
| **Missed wakeup** | With the `notify` removed, the consumer took one item and then hung forever | A notify only helps a thread that is already asleep, so a thread that sleeps after the last notify waits forever | Always wait with a predicate (the condition is checked before sleeping and after every wakeup) and always notify after changing state |
| **Compile errors from templates and threads** | `lock_guard` rejected a `const` mutex, `std::thread` rejected a pointer where a reference was needed, and `cv.wait` rejected a `lock_guard` | Locking modifies the mutex, `std::thread` copies its arguments, and `wait` must unlock and relock | Made the mutex `mutable`, passed the queue with `std::ref`, and switched to `std::unique_lock` for waiting. I learned to read template errors by finding the first `error:` line and the `required from here` line in my own file |
| **Shutdown hang** | The consumer slept forever once the producer was done, and a wrong predicate (`!empty && closed`) deadlocked both threads | Nothing told the consumer that no more items were coming, and the predicate required the wrong condition | Added `close()`. The predicate is `!empty \|\| closed`, a consumer exits only when the queue is empty **and** closed, and `close()` calls `notify_all()` on both condition variables |
| **Capacity check that never fired** | `Queue<int> q(0)` was accepted | I compared an unsigned `size_t` with `< 0`, which is never true | Changed the check to `== 0`. Compiling with `-Wall -Wextra` flags this kind of mistake |
| **Misleading logs** | A capacity-2 queue appeared to hold 3 items | Log lines printed after `push` and `pop` returned, outside the lock, so they were out of order with the real queue operations | Moved the logging inside the lock, so the log shows the true order of operations |
| **Garbled console output** | Output from several threads interleaved mid-line | Each `<<` is a separate write to `std::cout` | Build the line in an `ostringstream` first, then write it once under a small output mutex |
| **Thread pool hang and early "done"** | Joining the workers before closing the queue hung, and "ALL DONE" printed while tasks were still running | Workers sleep on an empty queue until it closes, and the pool's destructor only runs at the end of its scope | The destructor closes first and then joins. The demo creates the pool in its own `{ }` scope so the destructor finishes before "ALL DONE" prints |

## Notes

- **Task order:** tasks are *started* in FIFO order, but they finish and print in whatever order the scheduler runs the workers. That is expected from a pool. Tasks that need an order need an explicit dependency.
- **`try_pop` and `close()`:** `try_pop` never blocks, so it returns `false` for "empty" regardless of whether the queue is closed.