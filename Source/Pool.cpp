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

#include "stdafx.h"

#include <Pool.h>
#include <thread>

using namespace pool;

class CThreadPool : public IThreadPool
{

protected:

	struct STaskInfo
	{
		STaskInfo(TASK_CALLBACK task, LPVOID param0, LPVOID param1, size_t task_number, size_t *pactionref) :
			m_Task(task),
			m_pActionRef(pactionref)
		{
			m_Param[0] = param0;
			m_Param[1] = param1;
			m_TaskNumber = task_number;

			if (m_pActionRef)
			{
				(*m_pActionRef)++;
			}
		}

		// The function that the thread should be running
		TASK_CALLBACK m_Task;

		// The parameter given to the thread function
		LPVOID m_Param[2];
		size_t m_TaskNumber;

		// This is the number of active tasks, used for blocking
		size_t *m_pActionRef;
	};

	typedef std::deque<STaskInfo> TTaskList;

	TTaskList m_TaskList;

	CRITICAL_SECTION m_csTaskList;

	bool GetNextTask(STaskInfo &task)
	{
		bool ret = false;

		EnterCriticalSection(&m_csTaskList);

		if (!m_TaskList.empty())
		{
			task = m_TaskList.front();
			m_TaskList.pop_front();
			ret = true;
		}

		LeaveCriticalSection(&m_csTaskList);

		return ret;
	}

	void WorkerThreadProc()
	{
		while (true)
		{
			DWORD waitret = WaitForMultipleObjects(TS_NUMSEMAPHORES, m_hSemaphores, false, INFINITE) - WAIT_OBJECT_0;
			if (waitret == TS_QUIT)
				break;

			STaskInfo task(nullptr, nullptr, nullptr, 0, nullptr);
			while (GetNextTask(task))
			{
				task.m_Task(task.m_Param[0], task.m_Param[1], task.m_TaskNumber);

				if (task.m_pActionRef)
				{
					(*(task.m_pActionRef))--;
				}

				Sleep(0);
			}
		}
	}

	static void _WorkerThreadProc(CThreadPool *param)
	{
		CThreadPool *_this = (CThreadPool *)param;
		_this->WorkerThreadProc();
	}

	// the actual thread handles... keep them separated from SThreadInfo
	// so we can wait on them.
	std::vector<std::thread> m_hThreads;

	enum
	{
		TS_QUIT = 0,		// indicates it's time for a thread to shut down
		TS_RUN,				// indicates that the thread should start running the function given

		TS_NUMSEMAPHORES
	};

	HANDLE m_hSemaphores[TS_NUMSEMAPHORES];

public:
	CThreadPool(UINT threads_per_core, INT core_count_adjustment)
	{
		// Find out how many cores the system has
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);

		// Calculate the number of threads we need and allocate thread handles
		m_hThreads.resize(threads_per_core * max(1, (sysinfo.dwNumberOfProcessors + core_count_adjustment)));

		InitializeCriticalSection(&m_csTaskList);

		// this is the quit semaphore...
		m_hSemaphores[TS_QUIT] = CreateSemaphore(NULL, 0, (LONG)m_hThreads.size(), NULL);

		// this is the run semaphore...
		m_hSemaphores[TS_RUN] = CreateSemaphore(NULL, 0, (LONG)m_hThreads.size(), NULL);

		for (size_t i = 0; i < m_hThreads.size(); i++)
		{
			m_hThreads[i] = std::thread(_WorkerThreadProc, this);
		}
	}

	CThreadPool(size_t thread_count)
	{
		// Calculate the number of threads we need and allocate thread handles
		m_hThreads.resize(thread_count);

		InitializeCriticalSection(&m_csTaskList);

		// this is the quit semaphore...
		m_hSemaphores[TS_QUIT] = CreateSemaphore(NULL, 0, (LONG)m_hThreads.size(), NULL);

		// this is the run semaphore...
		m_hSemaphores[TS_RUN] = CreateSemaphore(NULL, 0, (LONG)m_hThreads.size(), NULL);

		for (size_t i = 0; i < m_hThreads.size(); i++)
		{
			m_hThreads[i] = std::thread(_WorkerThreadProc, this);
		}
	}

	virtual ~CThreadPool()
	{
		PurgeAllPendingTasks();

		ReleaseSemaphore(m_hSemaphores[TS_QUIT], (LONG)m_hThreads.size(), NULL);

		for (size_t i = 0; i < m_hThreads.size(); i++)
		{
			m_hThreads[i].join();
		}

		CloseHandle(m_hSemaphores[TS_QUIT]);
		CloseHandle(m_hSemaphores[TS_RUN]);

		DeleteCriticalSection(&m_csTaskList);
	}

	virtual void Release()
	{
		delete this;
	}

	// the number of worker threads in the pool
	virtual size_t GetNumThreads()
	{
		return (UINT)m_hThreads.size();
	}

	virtual bool RunTask(TASK_CALLBACK func, LPVOID param0 = nullptr, LPVOID param1 = nullptr, size_t numtimes = 1, bool block = false)
	{
		size_t blockwait = 0;

		EnterCriticalSection(&m_csTaskList);

		for (size_t i = 0; i < numtimes; i++)
		{
			m_TaskList.push_back(STaskInfo(func, param0, param1, i, block ? &blockwait : NULL));
		}

		LeaveCriticalSection(&m_csTaskList);

		ReleaseSemaphore(m_hSemaphores[TS_RUN], (LONG)m_hThreads.size(), NULL);

		if (block)
		{
			while (blockwait)
			{
				Sleep(1);
			}
		}

		return true;
	}

	virtual void WaitForAllTasks(DWORD milliseconds)
	{
		while (!m_TaskList.empty())
		{
			Sleep(1);
		}
	}

	virtual void PurgeAllPendingTasks()
	{
		EnterCriticalSection(&m_csTaskList);

		m_TaskList.clear();

		LeaveCriticalSection(&m_csTaskList);
	}
};

// Creates a pool with the number of threads based on the cores in the machine, given by:
//   threads_per_core * max(1, (core_count + core_count_adjustment))
IThreadPool *IThreadPool::Create(UINT threads_per_core, INT core_count_adjustment)
{
	return new CThreadPool(threads_per_core, core_count_adjustment);
}

// Creates a pool with only the designated number of threads
IThreadPool *IThreadPool::Create(size_t thread_count)
{
	return new CThreadPool(thread_count);
}
