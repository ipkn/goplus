#pragma once

#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <iostream>

#include "goroutine.h"

namespace goplus
{
	namespace detail 
	{
        // TODO haha global queue?
        std::mutex global_lock;
        std::deque<goroutine*> global_queue;

        bool done{false};
        int global_count = 0;

        thread_local goroutine scheduler_context;
        thread_local goroutine* scheduler_current;
	}

	class scheduler
	{
    private:
        std::vector<std::thread> threads_;
        scheduler()
        {
            int num_thread = GOPLUS_CONCURRENCY;
            if (num_thread < 1)
                num_thread = std::thread::hardware_concurrency();
			int initializing = num_thread;

            for(int i = 0; i < num_thread; i ++)
                queues_.emplace_back();

			std::condition_variable cv;
			std::mutex cv_mtx;

            for(int i = 0; i < num_thread; i ++)
            {
                threads_.emplace_back([&, i]{

                    {
                        boost::context::execution_context<void> x(
                                [&](boost::context::execution_context<void> sink)mutable{
                                detail::scheduler_context.set(sink);
                                detail::scheduler_context.set_scheduler(i);
                                return detail::scheduler_context.detach();});
                        x();
                    }
                    std::mutex local_lock;
                    auto& q = queues_[i];
                    q.lock = &local_lock;
					{
						cv_mtx.lock();
						initializing --;
						cv_mtx.unlock();
						cv.notify_one();
					}

                    while(!detail::done || !q.queue.empty() || !detail::global_queue.empty())
                    {
                        if (!q.queue.empty())
                        {
                            q.lock->lock();
                            if (!q.queue.empty())
                            {
                                detail::scheduler_current = q.queue.front();
                                q.queue.pop_front();
                                q.lock->unlock();
                                //std::cerr << "l run " << i << std::endl;
                                detail::scheduler_current->execute();
                            }
                            else
                            {
                                q.lock->unlock();
                            }
                        }
                        if (!detail::global_queue.empty())
                        {
                            detail::global_lock.lock();
                            if (!detail::global_queue.empty())
                            {
                                goroutine* r = detail::global_queue.front();
                                r->set_scheduler(i);
                                detail::global_queue.pop_front();
                                detail::global_lock.unlock();
								detail::scheduler_current = r;
                                //std::cerr << "g run " << i << std::endl;
                                detail::scheduler_current->execute();
                            }
                            else
                            {
                                detail::global_lock.unlock();
                            }
                        }
                        std::this_thread::yield();
                    } 
                    q.dead = true;
                    q.lock = nullptr;
                });
            }
			while(initializing)
			{
				std::unique_lock<std::mutex> lock(cv_mtx);
				cv.wait(lock);
			}
        }
        ~scheduler()
        {
            for(auto& t:threads_)
                t.join();
        }

        static scheduler& instance()
        {
            static scheduler s;
            return s;
        }
    public:
        template <typename F>
        static void spawn(F f)
        {
            scheduler::instance();
            detail::global_lock.lock();
            detail::global_count ++;
            detail::global_queue.emplace_back(new goroutine(
						[f](boost::context::execution_context<void> sink)mutable{
							detail::scheduler_context.set(sink);
							f(); 
                            {
                                std::unique_lock<std::mutex> u(detail::global_lock);
                                detail::global_count --;
                                if (detail::global_count == 0)
                                    detail::done = true;
                            }
                            return detail::scheduler_context.detach();
                        }
                        ));
            detail::global_lock.unlock();
        }

		static void resume(goroutine& go)
        {
            if (go.get_scheduler() >= 0)
            {
                auto& q = instance().queues_[go.get_scheduler()];
                q.lock->lock();
                q.queue.push_back(&go);
                q.lock->unlock();
            }
            else
            {
                detail::global_lock.lock();
                detail::global_queue.push_back(&go);
                detail::global_lock.unlock();
            }
        }
		static goroutine* current()
        {
			return detail::scheduler_current;
        }
		static void park()
        {
			auto save = (detail::scheduler_current);
            detail::scheduler_context.execute();
			detail::scheduler_current = (save);
        }
    private:
        struct local_queue_t
        {
            std::mutex* lock;
            std::deque<goroutine*> queue;
            double dummy;
            bool dead{false};

            local_queue_t(){};
            local_queue_t(local_queue_t&&) =  default;
            local_queue_t& operator = (local_queue_t&&) = default;
        };
        std::vector<local_queue_t> queues_;
	};
};
