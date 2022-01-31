/*

	Pool, a thread-pooled asynchronous job library

	Copyright © 2009-2022, Keelan Stuart. All rights reserved.

	Pool is free software; you can redistribute it and/or modify it under
	the terms of the MIT License:

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.

*/

#pragma once

// If you use the static library version of Pool, you must define POOL_STATIC in your project's preprocessor definitions

#if !defined(POOL_STATIC)

#ifdef POOL_EXPORTS
#define POOL_API __declspec(dllexport)
#else
#define POOL_API __declspec(dllimport)
#endif

#else

#define POOL_API

#endif


#include <stdint.h>


namespace pool
{

class IThreadPool
{
public:

	typedef enum
	{
		TR_OK = 0,		// the task is done, no special behavior

		TR_RERUN,		// the task should be re-run immediately
		TR_REQUEUE,		// the task should be re-queued
	} TASK_RETURN;

	// param0 and param1 are user-supplied values
	// task_number is, for tasks started by RunTask where numtimes > 1, correspondent to the number of times
	// the task has previously been run in this go.
	typedef TASK_RETURN (__cdecl *TASK_CALLBACK)(void *param0, void *param1, size_t task_number);

	// Deletes the underlying thread pool and frees any resources associated with it
	virtual void Release() = NULL;

	// Returns the number of worker threads in the pool
	virtual size_t GetNumThreads() = NULL;

	// Runs a task in the background, once or multiple times, optionally blocking.
	// For example, if one wished to run 1000 identical tasks
	virtual bool RunTask(TASK_CALLBACK func, void *param0 = nullptr, void *param1 = nullptr, size_t numtimes = 1, bool block = false) = NULL;

	// Waits for all active tasks to complete, until milliseconds expires... or INFINITE to wait forever
	// NOTE: new task submission is still allowed during this function, so refrain from running new tasks to return
	virtual void WaitForAllTasks(uint32_t milliseconds) = NULL;

	// Removes any tasks not already running from the queue
	virtual void PurgeAllPendingTasks() = NULL;

	// Executes all tasks immediately on the calling thread, ideal for task queues as opposed to thread pools (use this mode with 0 threads)
	virtual void Flush() = NULL;

	// Creates a pool with the number of threads based on the cores in the machine, given by:
	//    threads_per_core * max(1, (core_count + core_count_adjustment))
	POOL_API static IThreadPool *Create(size_t threads_per_core, int core_count_adjustment);

	// Creates a pool with only the designated number of threads
	// NOTE: A pool with 0 threads may be created where tasks can be emitted by multiple threads but consumed
	//    by just one. This model is particularly useful for things like updating textures in your graphics
	//    thread, using data provided by async resource workers.
	POOL_API static IThreadPool *Create(size_t thread_count);

};


};
