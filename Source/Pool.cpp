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

#include <Pool.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <limits>

using namespace pool;


class ThreadPool : public IThreadPool
{

	struct Task
	{
		Task(pool::IThreadPool::PoolFunc func, size_t task_number, std::atomic<size_t> *pactionref) :
			m_pActionRef(pactionref), m_Func(func)
		{
			m_TaskNumber = task_number;

			if (m_pActionRef)
				(*m_pActionRef)++;
		}

		// This is the number of active tasks, used for blocking
		std::atomic<size_t> *m_pActionRef;

		// The function that the thread should be running
		pool::IThreadPool::PoolFunc m_Func;

		// The parameter given to the thread function
		size_t m_TaskNumber;
	};

public:

	explicit ThreadPool(size_t thread_count)
    {
        m_Workers.reserve(thread_count);

		for (size_t i = 0; i < thread_count; ++i)
            m_Workers.emplace_back([this]() { WorkerLoop(); });
    }


    ThreadPool(size_t threads_per_core, int core_count_adjustment)
        : ThreadPool(CalculateThreadCount(threads_per_core, core_count_adjustment))
    {
    }


    virtual ~ThreadPool()
	{
		m_Stopping = true;
		PurgeAllPendingTasks();
		WaitForAllTasks();

		m_WorkCondition.notify_all();
        
		for (auto& worker : m_Workers)
            if (worker.joinable())
				worker.join();
    }


    void Release()
	{
		delete this;
	}


	size_t GetNumThreads()
	{
		return m_Workers.size();
	}


	bool RunTask(PoolFunc func, size_t numtimes, bool block) override
    {
		// this should probably just assert......
        if (!func || numtimes == 0)
			return false;

		if (m_Stopping)
			return false;

		// when we're blocking, we have an atomic counter...
		// Task will auto-increment the count during construction.
		// Execute (and Purge) will decrement it.
		std::atomic<size_t> block_count = 0;
		std::atomic<size_t> *pbc = block ? &block_count : nullptr;

		{
            std::lock_guard<std::mutex> lock(m_Mutex);

			for (size_t i = 0; i < numtimes; i++)
                m_Tasks.push_back(Task{func, i, pbc});
        }

		m_WorkCondition.notify_all();

		// Sometimes, you may have no workers (it's just a work queue), as you might
		// with a graphics rendering system where there is a drawing thread.
		// This handles that case.
        if (m_Workers.empty())
        {
            if (block)
				Flush();
        }
        else if (block)
        {
			// get the inital count we're waiting for...
			size_t cur_count = block_count.load();

			while (cur_count)
			{
				// atomic::wait will wait for the value to change...
				block_count.wait(cur_count);

				// ...so after any change, we get the new value and wait for
				// that to change.
				cur_count = block_count.load();
			}
        }

		return true;
    }


    void WaitForAllTasks(uint32_t milliseconds = WAIT_FOREVER)
    {
        if (m_Workers.empty())
        {
            Flush();
            return;
        }

        std::unique_lock<std::mutex> lock(m_Mutex);

		auto done = [&]()
		{
			return m_Tasks.empty();
		};

		if (milliseconds == std::numeric_limits<uint32_t>::max())
            m_IdleCondition.wait(lock, done);
        else
            m_IdleCondition.wait_for(lock, std::chrono::milliseconds(milliseconds), done);
    }


	void PurgeAllPendingTasks()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

		// make sure that any tasks still waiting to be run that might be blocking
		// have that reference decremented - otherwise, 
		for (auto &task : m_Tasks)
		{
			if (task.m_pActionRef)
			{
				(*(task.m_pActionRef))--;
				task.m_pActionRef->notify_all();
			}
		}

		m_Tasks.clear();
    }


	void Flush()
    {
		if (!m_Workers.empty())
			return;

		while (true)
        {
			std::unique_lock<std::mutex> lock(m_Mutex);
			
			if (m_Tasks.empty())
				break;
			
			Task task = std::move(m_Tasks.front());
			m_Tasks.pop_front();
			lock.unlock();

			Execute(task);
        }
    }


private:

	static size_t CalculateThreadCount(size_t per_core, int adjustment)
	{
		int cores = std::max<int>(1, std::thread::hardware_concurrency());

		return per_core * std::max<int>(1, cores + adjustment);
	}


	void Execute(Task &task)
	{
		TaskReturn result;

		do
		{
			result = task.m_Func(task.m_TaskNumber);
		}
		while ((result == TaskReturn::RERUN) && !m_Stopping);

		if (result == TaskReturn::REQUEUE)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);

			if (!m_Stopping)
			{
				m_Tasks.push_back(std::move(task));
				m_WorkCondition.notify_one();
				return;
			}
		}

		if (task.m_pActionRef)
		{
			(*(task.m_pActionRef))--;
			task.m_pActionRef->notify_all();
		}
	}


	void WorkerLoop()
    {
		while (true)
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			
			m_WorkCondition.wait(lock, [&]()
			{
				return m_Stopping || !m_Tasks.empty();
			});
			
			if (m_Stopping)
				return;
			
			Task task = std::move(m_Tasks.front());
			m_Tasks.pop_front();
			lock.unlock();

			Execute(task);

			if (m_Tasks.empty())
				m_IdleCondition.notify_all();
		}
    }


    std::deque<Task> m_Tasks;
    std::vector<std::thread> m_Workers;
    std::mutex m_Mutex;
    std::condition_variable m_WorkCondition;
    std::condition_variable m_IdleCondition;
    bool m_Stopping = false;
};


pool::IThreadPool* pool::IThreadPool::Create(size_t threads_per_core, int core_count_adjustment)
{
    return new ThreadPool(threads_per_core, core_count_adjustment);
}


pool::IThreadPool* pool::IThreadPool::Create(size_t thread_count)
{
    return new ThreadPool(thread_count);
}
