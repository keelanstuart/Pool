/*
	Pool, a thread-pooled asynchronous job library

	Copyright © 2009-2025, Keelan Stuart. All rights reserved.

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

#if defined(_WIN32)

#include <windows.h>
#include <synchapi.h>
#define sem_t HANDLE

#elif defined(__linux__)

#include <semaphore.h>

#endif

#include <malloc.h>
#include <memory.h>
#include <queue>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>

#include <Pool.h>

using namespace pool;

class CThreadPool : public IThreadPool
{

protected:

	__declspec(align(64)) struct STaskInfo
	{
		STaskInfo(PoolFunc task, size_t task_number, volatile LONG *pactionref) :
			m_pActionRef(pactionref), m_Task(task)
		{
			m_TaskNumber = task_number;

			if (m_pActionRef)
			{
				InterlockedIncrement(m_pActionRef);
			}
		}

		// This is the number of active tasks, used for blocking
		volatile LONG *m_pActionRef;

		// The function that the thread should be running
		PoolFunc m_Task;

		// The parameter given to the thread function
		size_t m_TaskNumber;
	};

	typedef std::queue<STaskInfo> TTaskQueue;

	TTaskQueue m_TaskQueue;

	std::mutex m_mutexTaskList;

	bool GetNextTask(STaskInfo &task)
	{
		// lock the queue
		std::lock_guard<std::mutex> l(m_mutexTaskList);

		// return a task if one is available
		if (!m_TaskQueue.empty())
		{
			task = m_TaskQueue.front();
			m_TaskQueue.pop();
			return true;
		}

		return false;
	}

	void WorkerThreadProc()
	{
		while (true)
		{
			// wait until told to run or quit
			DWORD waitret = WaitForMultipleObjects(TS_NUMSEMAPHORES, m_hSemaphores, false, INFINITE) - WAIT_OBJECT_0;
			if (waitret == TS_QUIT)
				break;

			STaskInfo task(nullptr, 0, nullptr);
			while (true)
			{
				if (!GetNextTask(task))
					break;

				TASK_RETURN ret;

				// run the task as long as it keeps telling us to re-run
				do
				{
					ret = task.m_Task(task.m_TaskNumber);
				}
				while (ret == TASK_RETURN::TR_RERUN);

				// if we need to re-queue it, do that now
				if (ret == TASK_RETURN::TR_REQUEUE)
				{
					m_mutexTaskList.lock();

					m_TaskQueue.push(task);

					m_mutexTaskList.unlock();

					if (m_hSemaphores[TS_RUN])
						ReleaseSemaphore(m_hSemaphores[TS_RUN], (LONG)m_hThreads.size(), NULL);
				}
				// otherwise, indicate that the action has completed
				else if (task.m_pActionRef)
				{
					InterlockedDecrement(task.m_pActionRef);
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

		if (thread_count)
		{
			m_hThreads.resize(thread_count);

			// create the run and quit semaphore...
#if defined(_WIN32)
			m_hSemaphores[TS_QUIT] = CreateSemaphore(NULL, 0, (LONG)m_hThreads.size(), NULL);
			m_hSemaphores[TS_RUN] = CreateSemaphore(NULL, 0, (LONG)m_hThreads.size(), NULL);
#elif defined(__linux__)
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

#if defined(_WIN32)
			ReleaseSemaphore(m_hSemaphores[TS_QUIT], (LONG)m_hThreads.size(), NULL);
#elif defined(__linux__)
#endif

			for (size_t i = 0; i < m_hThreads.size(); i++)
			{
				m_hThreads[i].join();
			}

#if defined(_WIN32)
			CloseHandle(m_hSemaphores[TS_QUIT]);
			CloseHandle(m_hSemaphores[TS_RUN]);
#elif defined(__linux__)
			sem_destroy(&m_hSemaphores[TS_QUIT]);
			sem_destroy(&m_hSemaphores[TS_RUN]);
#endif
		}

		memset(m_hSemaphores, 0, sizeof(HANDLE) * TS_NUMSEMAPHORES);
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

	virtual bool RunTask(PoolFunc func, size_t numtimes = 1, bool block = false)
	{
		// if blocking is desired, blockwait will be incremented by each STaskInfo
		volatile LONG blockwait = 0;

		{
			std::lock_guard<std::mutex> l(m_mutexTaskList);

			for (size_t i = 0; i < numtimes; i++)
			{
				m_TaskQueue.push(STaskInfo(func, i, block ? &blockwait : nullptr));
			}
		}

		// tell the threads to run tasks
		if (m_hSemaphores[TS_RUN])
			ReleaseSemaphore(m_hSemaphores[TS_RUN], (LONG)m_hThreads.size(), nullptr);

		// if we wanted to block, then wait until all of the tasks have completed (ie., wait until blockwait is 0 again)
		if (m_hThreads.size() && block)
		{
			while (blockwait)
			{
				Sleep(10);
			}
		}

		return true;
	}

	virtual void WaitForAllTasks(uint32_t milliseconds)
	{
		if (m_hThreads.size())
		{
			while (!m_TaskQueue.empty())
			{
				Sleep(0);
			}
		}
		else
		{
			Flush();
		}
	}

	virtual void PurgeAllPendingTasks()
	{
		std::lock_guard<std::mutex> l(m_mutexTaskList);

		// clear the queue (there is no .clear() method, so this is the way)
		m_TaskQueue = { };
	}

	virtual void Flush()
	{
		std::lock_guard<std::mutex> l(m_mutexTaskList);

		while (!m_TaskQueue.empty())
		{
			STaskInfo &t = m_TaskQueue.front();
			t.m_Task(t.m_TaskNumber);

			if (t.m_pActionRef)
			{
				InterlockedDecrement(t.m_pActionRef);
			}

			m_TaskQueue.pop();
		}
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
