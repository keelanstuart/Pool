/*
	Pool, a thread-pooled asynchronous job library

	Copyright © 2009-2019, Keelan Stuart. All rights reserved.

	Pool is free software; you can redistribute it and/or modify it under
	the terms of the GNU Lesser General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	Pool is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Lesser General Public License for more details.
	See <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <stdint.h>


#ifdef POOL_EXPORTS
#define POOL_API __declspec(dllexport)
#else
#define POOL_API __declspec(dllimport)
#endif


namespace pool
{

class IThreadPool
{
public:
	// param0 and param1 are user-supplied values
	// task_number is, for tasks started by RunTask where numtimes > 1, correspondent to the number of times
	// the task has previously been run in this go.
	typedef void (WINAPI *TASK_CALLBACK)(LPVOID param0, LPVOID param1, size_t task_number);

	// Deletes the underlying thread pool and frees any resources associated with it
	virtual void Release() = NULL;

	// Returns the number of worker threads in the pool
	virtual size_t GetNumThreads() = NULL;

	// Runs a task in the background, once or multiple times, optionally blocking.
	// For example, if one wished to run 1000 identical tasks
	virtual bool RunTask(TASK_CALLBACK func, LPVOID param0 = nullptr, LPVOID param1 = nullptr, size_t numtimes = 1, bool block = false) = NULL;

	// Waits for all active tasks to complete, until milliseconds expires... or INFINITE to wait forever
	virtual void WaitForAllTasks(DWORD milliseconds) = NULL;

	// Removes any tasks not already running from the queue
	virtual void PurgeAllPendingTasks() = NULL;

	// Creates a pool with the number of threads based on the cores in the machine, given by:
	//   threads_per_core * max(1, (core_count + core_count_adjustment))
	POOL_API static IThreadPool *Create(UINT threads_per_core, INT core_count_adjustment);

	// Creates a pool with only the designated number of threads
	POOL_API static IThreadPool *Create(size_t thread_count);

};


};
