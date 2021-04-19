/*

	Pool, a thread-pooled asynchronous job library

	Copyright © 2009-2021, Keelan Stuart. All rights reserved.

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

#if defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#if defined(_MSC_BUILD)

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

#include <windows.h>
#include <synchapi.h>
#define sem_t HANDLE

#elif defined(LINUX)

#include <semaphore.h>

#endif

#include <malloc.h>
#include <memory.h>
#include <deque>
#include <vector>
#include <algorithm>
#include <thread>


#include <Pool.h>
#include <thread>

using namespace pool;

class CThreadPool : public IThreadPool
{

protected:

	struct STaskInfo
	{
		STaskInfo(TASK_CALLBACK task, void *param0, void *param1, size_t task_number, size_t *pactionref) :
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
		void *m_Param[2];
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
				TASK_RETURN ret;
				do
				{
					ret = task.m_Task(task.m_Param[0], task.m_Param[1], task.m_TaskNumber);
				}
				while (ret == TASK_RETURN::TR_RERUN);

				if (ret == TASK_RETURN::TR_REQUEUE)
				{
					EnterCriticalSection(&m_csTaskList);

					m_TaskList.push_back(task);

					LeaveCriticalSection(&m_csTaskList);
				}
				else if (task.m_pActionRef)
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

	sem_t m_hSemaphores[TS_NUMSEMAPHORES];

public:

	void Initialize(size_t thread_count)
	{
		memset(m_hSemaphores, 0, sizeof(HANDLE) * TS_NUMSEMAPHORES);

		InitializeCriticalSection(&m_csTaskList);

		if (thread_count)
		{
			m_hThreads.resize(thread_count);

			// create the run and quit semaphore...
#if defined(_MSC_BUILD)
			m_hSemaphores[TS_QUIT] = CreateSemaphore(NULL, 0, (LONG)m_hThreads.size(), NULL);
			m_hSemaphores[TS_RUN] = CreateSemaphore(NULL, 0, (LONG)m_hThreads.size(), NULL);
#elif defined(LINUX)
			sem_init(&m_hSemaphores[TS_QUIT], 0, m_hThreads.size());
			sem_init(&m_hSemaphores[TS_QUIT], 0, m_hThreads.size());
#endif

			for (size_t i = 0; i < m_hThreads.size(); i++)
			{
				m_hThreads[i] = std::thread(_WorkerThreadProc, this);
			}
		}
	}

	CThreadPool(size_t threads_per_core, int core_count_adjustment)
	{
		// Find out how many cores the system has
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);

		// Calculate the number of threads we need and allocate thread handles
		Initialize(threads_per_core * (size_t)std::max<int>(1, (sysinfo.dwNumberOfProcessors + core_count_adjustment)));
	}

	CThreadPool(size_t thread_count)
	{
		Initialize(thread_count);
	}

	virtual ~CThreadPool()
	{
		if (m_hThreads.size())
		{
			PurgeAllPendingTasks();

#if defined(_MSC_BUILD)
			ReleaseSemaphore(m_hSemaphores[TS_QUIT], (LONG)m_hThreads.size(), NULL);
#elif defined(LINUX)
#endif

			for (size_t i = 0; i < m_hThreads.size(); i++)
			{
				m_hThreads[i].join();
			}

#if defined(_MSC_BUILD)
			CloseHandle(m_hSemaphores[TS_QUIT]);
			CloseHandle(m_hSemaphores[TS_RUN]);
#elif defined(LINUX)
			sem_destroy(&m_hSemaphores[TS_QUIT]);
			sem_destroy(&m_hSemaphores[TS_RUN]);
#endif
		}

		memset(m_hSemaphores, 0, sizeof(HANDLE) * TS_NUMSEMAPHORES);

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

	virtual bool RunTask(TASK_CALLBACK func, void *param0 = nullptr, void *param1 = nullptr, size_t numtimes = 1, bool block = false)
	{
		size_t blockwait = 0;

		EnterCriticalSection(&m_csTaskList);

		for (size_t i = 0; i < numtimes; i++)
		{
			m_TaskList.push_back(STaskInfo(func, param0, param1, i, block ? &blockwait : NULL));
		}

		LeaveCriticalSection(&m_csTaskList);

		if (m_hSemaphores[TS_RUN])
			ReleaseSemaphore(m_hSemaphores[TS_RUN], (LONG)m_hThreads.size(), NULL);

		if (m_hThreads.size() && block)
		{
			while (blockwait)
			{
				Sleep(1);
			}
		}

		return true;
	}

	virtual void WaitForAllTasks(uint32_t milliseconds)
	{
		if (m_hThreads.size())
		{
			while (!m_TaskList.empty())
			{
				Sleep(1);
			}
		}
		else
		{
			Flush();
		}
	}

	virtual void PurgeAllPendingTasks()
	{
		EnterCriticalSection(&m_csTaskList);

		m_TaskList.clear();

		LeaveCriticalSection(&m_csTaskList);
	}

	virtual void Flush()
	{
		EnterCriticalSection(&m_csTaskList);

		for (TTaskList::const_iterator it = m_TaskList.cbegin(), last_it = m_TaskList.cend(); it != last_it; it++)
		{
			const STaskInfo *task = &(*it);

			task->m_Task(task->m_Param[0], task->m_Param[1], task->m_TaskNumber);

			if (task->m_pActionRef)
			{
				(*(task->m_pActionRef))--;
			}
		}
		m_TaskList.clear();

		LeaveCriticalSection(&m_csTaskList);
	}
};

// Creates a pool with the number of threads based on the cores in the machine, given by:
//   threads_per_core * max(1, (core_count + core_count_adjustment))
IThreadPool *IThreadPool::Create(size_t threads_per_core, int core_count_adjustment)
{
	return new CThreadPool(threads_per_core, core_count_adjustment);
}

// Creates a pool with only the designated number of threads
IThreadPool *IThreadPool::Create(size_t thread_count)
{
	return new CThreadPool(thread_count);
}
