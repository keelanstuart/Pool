# Pool

**A small, flexible asynchronous task library for C++**

Pool is a lightweight C++ thread pool built around a deliberately simple idea:

> **Give it work. Let it figure out where to run it. Wait only when you need to.**

Create a pool, submit tasks, and let a reusable set of worker threads execute them asynchronously. Tasks can be submitted individually or many times, waited upon when synchronization is necessary, re-run or re-queued by the task itself, or discarded when queued work is no longer relevant.

Pool can also operate with **no worker threads at all**, turning it into a thread-safe, multi-producer task queue whose work is executed later on whichever thread calls `Flush()`.

---

## Why Pool?

Thread pools aren't complicated.

Using one shouldn't be, either.

Sometimes you simply want to say:

```cpp
thread_pool->RunTask([](size_t task_number)
{
    DoSomething();
    return pool::IThreadPool::OK;
});
```

and get on with the rest of your program.

Pool provides a small interface for:

* asynchronous task execution
* configurable worker counts
* running one operation many times in parallel
* waiting for outstanding work
* purging work that hasn't started yet
* re-running or re-queueing work from inside the task itself
* queueing work from many threads for later execution on one designated thread

There is no scheduler framework to adopt and no task graph to construct.

Submit work when you have it. Synchronize when you need to.

---

## Creating a thread pool

Creating a Pool returns an `IThreadPool` interface.

Create two worker threads per CPU core:

```cpp
pool::IThreadPool* thread_pool = pool::IThreadPool::Create(2, 0);
```

Create one worker per CPU core, but reduce the core count used in the calculation by three:

```cpp
pool::IThreadPool* thread_pool = pool::IThreadPool::Create(1, -3);
```

Or create an exact number of worker threads:

```cpp
pool::IThreadPool* thread_pool = pool::IThreadPool::Create(3);
```

That creates exactly three workers.

---

## Running asynchronous tasks

Tasks are `std::function` objects with this shape:

```cpp
pool::IThreadPool::TaskReturn(size_t task_number)
```

That means lambdas can capture whatever state they need directly:

```cpp
int value = 42;

thread_pool->RunTask([value](size_t task_number)
{
    Process(value);

    return pool::IThreadPool::OK;
});
```

There is no separate user-data pointer or callback context to manage.

Multiple kinds of work can be submitted to the same pool:

```cpp
thread_pool->RunTask([](size_t task_number)
{
    ProcessSomething();
    return pool::IThreadPool::OK;
}, 100);

thread_pool->RunTask([](size_t task_number)
{
    ProcessSomethingElse();
    return pool::IThreadPool::OK;
}, 10);
```

The calling thread continues while Pool works through the queue.

---

## Parallelizing one operation

A common threading problem isn't having many different jobs.

It's having **one operation that needs to happen many times**.

For example:

```cpp
thread_pool->RunTask([](size_t task_number)
{
    Process(task_number);

    return pool::IThreadPool::OK;
}, 1000, true);
```

This submits the same task 1,000 times.

Each invocation receives its own `task_number`, from which it can determine which portion of the larger operation to process.

The final `true` requests blocking behavior, so `RunTask()` does not return until all 1,000 invocations have completed.

The same operation without blocking is simply:

```cpp
thread_pool->RunTask([](size_t task_number)
{
    Process(task_number);

    return pool::IThreadPool::OK;
}, 1000);
```

---

## Task return values

Each task returns a `TaskReturn` value telling Pool what should happen next.

### `OK`

The task is complete:

```cpp
return pool::IThreadPool::OK;
```

This is the normal result.

### `RERUN`

Run the same task again immediately:

```cpp
return pool::IThreadPool::RERUN;
```

The task continues on the current worker without first being returned to the task queue.

This is useful when a task has more work it can perform immediately.

### `REQUEUE`

Put the task back into the queue:

```cpp
return pool::IThreadPool::REQUEUE;
```

The current invocation ends and the task becomes available to run again when a worker is next available.

This is useful when the task should yield its worker and try again later.

---

## Waiting for work

Asynchronous work doesn't always need immediate synchronization.

When it does:

```cpp
thread_pool->WaitForAllTasks();
```

waits until all outstanding work has completed.

A timeout can also be specified:

```cpp
thread_pool->WaitForAllTasks(1000);
```

which waits for up to 1,000 milliseconds.

To wait indefinitely:

```cpp
thread_pool->WaitForAllTasks(WAIT_FOREVER);
```

New task submissions are still allowed while `WaitForAllTasks()` is running, so applications that need the wait to eventually terminate should avoid continuously submitting new work during that period.

---

## Purging pending work

Sometimes queued work stops being useful.

An application may be shutting down, changing state, loading something different, or simply no longer care about operations that haven't started yet.

Pending tasks can be discarded:

```cpp
thread_pool->PurgeAllPendingTasks();
```

Tasks already executing are allowed to finish, while work still waiting in the queue is removed.

A common shutdown pattern is:

```cpp
thread_pool->PurgeAllPendingTasks();
thread_pool->WaitForAllTasks();
```

---

## A thread pool with no threads?

Yes.

And it's useful.

```cpp
pool::IThreadPool* deferred_tasks = pool::IThreadPool::Create(0);
```

A Pool with zero worker threads still accepts tasks normally.

Those tasks may be submitted concurrently from **any number of threads**:

```text
Worker Thread A ──┐
Worker Thread B ──┼──► Task Queue
Worker Thread C ──┘
```

Nothing executes them automatically because the pool has no worker threads.

Later, a designated thread calls:

```cpp
deferred_tasks->Flush();
```

and executes all queued work itself:

```text
Worker Thread A ──┐
Worker Thread B ──┼──► Task Queue ──► Flush() ──► Designated Thread
Worker Thread C ──┘
```

This makes a zero-thread Pool useful as a **multi-producer, single-consumer task queue**.

One common example is graphics programming.

Background threads can load, decode, or prepare resources and then submit only the work that must execute on the render thread:

```cpp
graphics_tasks->RunTask([texture](size_t task_number)
{
    UploadTextureToGPU(texture);

    return pool::IThreadPool::OK;
});
```

The producer thread does not need access to the graphics context.

Later, the render thread calls:

```cpp
graphics_tasks->Flush();
```

and executes all queued GPU work itself.

```text
Loading Thread ─────┐
Streaming Thread ───┼──► Graphics Task Queue
Asset Thread ───────┘            │
                                 ▼
                              Flush()
                                 │
                                 ▼
                           Render Thread
                                 │
                                 ▼
                           GPU operations
```

Same task interface.

Multiple producers.

One chosen execution thread.

---

## Capturing task state

Because tasks are `std::function` objects, task-specific state can be captured naturally:

```cpp
Mesh* mesh = LoadMesh();

graphics_tasks->RunTask([mesh](size_t task_number)
{
    mesh->UploadToGPU();

    return pool::IThreadPool::OK;
});
```

Or capture several values:

```cpp
thread_pool->RunTask([source, destination, count](size_t task_number)
{
    ProcessRange(source, destination, count, task_number);

    return pool::IThreadPool::OK;
}, worker_count);
```

The application remains responsible for ensuring that captured references and pointers remain valid until the task has finished.

---

## Querying the pool

The number of worker threads can be queried at runtime:

```cpp
size_t workers = thread_pool->GetNumThreads();
```

For a zero-thread task queue, this returns zero.

---

## Releasing a pool

When the pool is no longer needed:

```cpp
thread_pool->Release();
```

As with the rest of the library, lifetime management is explicit.

---

## A deliberately small abstraction

Pool isn't intended to turn asynchronous execution into a new programming model.

It's a reusable mechanism for putting work somewhere else:

```text
RunTask()
    │
    ▼
 Task Queue
    │
    ├──── Worker 1
    ├──── Worker 2
    ├──── Worker 3
    └──── ...
```

Or, when thread affinity matters:

```text
Multiple Threads
      │
      ▼
   RunTask()
      │
      ▼
  Task Queue
      │
      ▼
    Flush()
      │
      ▼
Calling Thread
```

The application decides what the work means and when synchronization matters.

Pool handles getting it executed.

---

## What Pool is — and isn't

Pool is intended for applications that need straightforward asynchronous or parallel execution without adopting a larger concurrency framework.

It works well when the problem sounds like:

> **"I have some work. I don't care which worker does it — I just need it done."**

Or:

> **"I need to run this operation a thousand times and use all the cores available."**

Or, in zero-thread mode:

> **"Work can come from anywhere, but this particular thread needs to execute it."**

Pool handles all three with the same interface.

---

## License

Pool is licensed under the MIT License.

Copyright © 2009-2026, Keelan Stuart.
