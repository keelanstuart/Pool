/*
	Pool, a thread-pooled asynchronous job library

	Copyright © 2009-2026, Keelan Stuart. All rights reserved.

	MIT License

	Permission is hereby granted, free of charge, to any person
	obtaining a copy of this software and associated documentation
	files (the "Software"), to deal in the Software without restriction,
	including without limitation the rights to use, copy, modify, merge,
	publish, distribute, sublicense, and/or sell copies of the Software,
	and to permit persons to whom the Software is furnished to do so,
	subject to the following conditions:

	The above copyright notice and this permission notice shall be
	included in all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
	CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
	TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
	SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>


// If you use the static library version of pool, you must define
// POOL_STATIC in your project's preprocessor definitions

#if defined(_WIN32) && !defined(POOL_STATIC)

#if defined(POOL_EXPORTS)

#define POOL_API __declspec(dllexport)

#else

#define POOL_API __declspec(dllimport)

#endif

#elif defined(__GNUC__) && !defined(POOL_STATIC)

#define POOL_API __attribute__((visibility("default")))

#else

#define POOL_API

#endif


// pool is a thread-pooling utility library that lets you issue tasks either to
// a task backlog which can be Flushed synchronously at your leisure or parceled out to
// a set of worker threads to be handled concurrently. It can also block on multi-task
// submissions, giving your task function a number

namespace pool
{
	class IThreadPool
	{

	public:

		using TaskReturn = enum
		{
			OK = 0,			// Return this to end the task
			RERUN,			// Return this to re-run the task immediately without re-queueing it
			REQUEUE			// Return this to re-queue the task so it runs at the next availabile time
		};

		// This used to be a callback with user data provided as [void *]... now you can
		// easily capture whatever data you want to with your task.
		using PoolFunc = std::function<TaskReturn(size_t task_number)>;

		// Deletes the underlying thread pool and frees any resources associated with it
		virtual void Release() = 0;

		// Returns the number of worker threads in the pool
		virtual size_t GetNumThreads() = 0;

		// Runs a task in the background, once or multiple times, optionally blocking.
		// For example, if one wished to run 1000 identical tasks
		virtual bool RunTask(PoolFunc func, size_t numtimes = 1, bool block = false) = 0;

		#define WAIT_FOREVER	-1

		// Waits for all active tasks to complete, until milliseconds expires... or INFINITE to wait forever
		// NOTE: new task submission is still allowed during this function, so refrain from running new tasks to return
		virtual void WaitForAllTasks(uint32_t milliseconds = WAIT_FOREVER) = 0;

		// Removes any tasks not already running from the queue
		virtual void PurgeAllPendingTasks() = 0;

		// Executes all tasks immediately on the calling thread, ideal for task queues as opposed to thread pools (use this mode with 0 threads)
		virtual void Flush() = 0;

		POOL_API static IThreadPool* Create(size_t threads_per_core, int core_count_adjustment);
		POOL_API static IThreadPool* Create(size_t thread_count);

	};
}
