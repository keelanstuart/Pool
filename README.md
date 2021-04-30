# Pool
Pool, a thread-pooled asynchronous job library with an easy-to-use API


****

#### Get Started: Create a Thread Pool

```C++
// Instantiates a pool and gives you an IThreadPool interface
// In this case, we spin up 2 threads per CPU core
IThreadPool *ppool1 = pool::IThreadPool::Create(2, 0);

// In this case, we spin up 1 thread per CPU core, but reduce the core count
// used to compute the thread count by 3
IThreadPool *ppool2 = pool::IThreadPool::Create(1, -3);

// In this case, we spin up 3 threads total
IThreadPool *ppool3 = pool::IThreadPool::Create(3);

// You can also create a "pool" with 0 threads, add tasks from multiple threads, then execute them all on a single thread later
// by calling Flush. This is useful for graphics tasks, for example, where you may want to load texture or geometry data
// asynchronously but then upload to GPU memory in the main render thread.
IThreadPool *pGraphicsTasks = pool::IThreadPool::Create(0);
```


****

#### A Simple Example: Asynchronous Processing

First, write your task callback(s)...
```C++
pool::IThreadPool::TASK_RETURN __cdecl SimpleTask1(void *param0, void *param1, size_t task_number)
{
  // do a thing - like Sleep(10)
  Sleep(10);

  return pool::IThreadPool::TASK_RETURN::TR_OK;
}

pool::IThreadPool::TASK_RETURN __cdecl SimpleTask2(void *param0, void *param1, size_t task_number)
{
  // do a different thing - like Sleep(50)
  Sleep(50);

  return pool::IThreadPool::TASK_RETURN::TR_OK;
}
```

Then, somewhere in your code, run some tasks...
```C++
for (int i = 0; i < 100; i++)
  ppool1->RunTask(SimpleTask1);

for (int i = 0; i < 10; i++)
  ppool1->RunTask(SimpleTask2);
```

Your tasks will now run, but will finish whenever they do - but, you can wait for them.
```C++
ppool1->WaitForAllTasks(INFINITE);
```

If your program's termination condition is variable and tasks may be left unfinished (and you don't want them to finish if it's time to quit), you can flush the task queue prior to waiting.
```C++
ppool1->PurgeAllPendingTasks();
```


****

#### Wrapping Up

```C++
ppool1->Release();
```
