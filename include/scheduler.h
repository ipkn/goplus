#pragma once

#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <iostream>

#include <boost/asio.hpp>

#include "goroutine.h"

//#define GOPLUS_DEBUG

namespace goplus
{
	namespace detail 
	{
        // TODO haha global queue?
        inline auto get_detail() {
            thread_local goroutine scheduler_context;
            thread_local goroutine* scheduler_current{};
            //thread_local std::mutex* 
            static std::mutex global_lock_;
            static std::deque<goroutine*> global_queue_;

            static bool done_{false};
            static int global_count_ = 0;

            struct VariableSet{
                std::mutex& global_lock{global_lock_};
                std::deque<goroutine*>& global_queue{global_queue_};

                bool& done{done_};
                int& global_count{global_count_};
                goroutine* scheduler_context;
                goroutine** scheduler_current;

                VariableSet(goroutine** scheduler_current, goroutine* scheduler_context)
                    : scheduler_context(scheduler_context), scheduler_current(scheduler_current)
                {
                }
                
            } variable_set(&scheduler_current, &scheduler_context);

            return variable_set;
        }
	}

	class scheduler
	{
    private:
        std::vector<std::thread> threads_;
        std::vector<boost::asio::io_service> io_services_;
        scheduler()
        {
            int num_thread = GOPLUS_CONCURRENCY;
            if (num_thread < 1)
                num_thread = std::thread::hardware_concurrency();
            std::atomic<int> initializing{num_thread};

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
                                detail::get_detail().scheduler_context->set(std::move(sink));
                                detail::get_detail().scheduler_context->set_scheduler(i);
                                return detail::get_detail().scheduler_context->detach();});
                        x();
                    }
                    std::mutex local_lock;
                    auto& q = queues_[i];
                    q.lock = &local_lock;
					{
						initializing --;
						cv.notify_one();
					}

                    while(!detail::get_detail().done || !q.queue.empty() || !detail::get_detail().global_queue.empty())
                    {
                        if (!q.queue.empty())
                        {
                            q.lock->lock();
                            if (!q.queue.empty())
                            {
                                *detail::get_detail().scheduler_current = q.queue.front();
                                q.queue.pop_front();
                                q.lock->unlock();
#ifdef GOPLUS_DEBUG
                                std::cerr << "l run " << i << std::endl;
#endif
                                (*detail::get_detail().scheduler_current)->execute();
                            }
                            else
                            {
                                q.lock->unlock();
                            }
                        }
                        if (!detail::get_detail().global_queue.empty())
                        {
                            detail::get_detail().global_lock.lock();
                            if (!detail::get_detail().global_queue.empty())
                            {
                                goroutine* r = detail::get_detail().global_queue.front();
                                r->set_scheduler(i);
                                detail::get_detail().global_queue.pop_front();
                                detail::get_detail().global_lock.unlock();
								*detail::get_detail().scheduler_current = r;
#ifdef GOPLUS_DEBUG
                                std::cerr << "g run " << i << std::endl;
#endif
                                (*detail::get_detail().scheduler_current)->execute();
                            }
                            else
                            {
                                detail::get_detail().global_lock.unlock();
                            }
                        }
                        std::this_thread::yield();
                    } 
                    q.dead = true;
                    q.lock = nullptr;
                });
            }
			while(initializing > 0)
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
            detail::get_detail().global_lock.lock();
            detail::get_detail().global_count ++;
            detail::get_detail().global_queue.emplace_back(new goroutine(
						[f](boost::context::execution_context<void> sink)mutable{
							detail::get_detail().scheduler_context->set(std::move(sink));
							f(); 
                            {
                                std::unique_lock<std::mutex> u(detail::get_detail().global_lock);
                                detail::get_detail().global_count --;
                                if (detail::get_detail().global_count == 0)
                                    detail::get_detail().done = true;
                            }
                            return detail::get_detail().scheduler_context->detach();
                        }
                        ));
            detail::get_detail().global_lock.unlock();
        }

        struct normal_thread_helper
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool wakeup{false};
        };

		static void resume(goroutine& go)
        {
            if (reinterpret_cast<std::intptr_t>(&go) & 1 == 1)
            {
#ifdef GOPLUS_DEBUG
                std::cerr << "normal notify" << std::endl;
#endif
                // normal thread
                normal_thread_helper* h = reinterpret_cast<normal_thread_helper*>(reinterpret_cast<std::intptr_t>(&go)&~(std::intptr_t)1);
                std::unique_lock<std::mutex> _(h->mtx);
                h->wakeup = true;
                h->cv.notify_one();
            }
            else if (go.get_scheduler() >= 0)
            {
                auto& q = instance().queues_[go.get_scheduler()];
                q.lock->lock();
                q.queue.push_back(&go);
                q.lock->unlock();
            }
            else
            {
                detail::get_detail().global_lock.lock();
                detail::get_detail().global_queue.push_back(&go);
                detail::get_detail().global_lock.unlock();
            }
        }
		static goroutine* current()
        {
            if (*detail::get_detail().scheduler_current == nullptr)
            {
#ifdef GOPLUS_DEBUG
                std::cerr << "null scheduler_current" << std::endl;
#endif
                *detail::get_detail().scheduler_current = reinterpret_cast<goroutine*>(
                       reinterpret_cast<std::intptr_t>(new normal_thread_helper) + 1 
                       );
            }
			return *detail::get_detail().scheduler_current;
        }
		static void park()
        {
            if (*detail::get_detail().scheduler_current == nullptr)
            {
#ifdef GOPLUS_DEBUG
                std::cerr << "null scheduler_current" << std::endl;
#endif
                *detail::get_detail().scheduler_current = reinterpret_cast<goroutine*>(
                       reinterpret_cast<std::intptr_t>(new normal_thread_helper) + 1 
                       );
            }
            if (reinterpret_cast<std::intptr_t>(*detail::get_detail().scheduler_current) & 1 == 1)
            {
                normal_thread_helper* h = reinterpret_cast<normal_thread_helper*>(reinterpret_cast<std::intptr_t>(
                            *detail::get_detail().scheduler_current
                            )&~(std::intptr_t)1);
                std::unique_lock<std::mutex> lock{h->mtx};
#ifdef GOPLUS_DEBUG
                std::cerr << "normal wait" << std::endl;
#endif
                while(!h->wakeup)
                    h->cv.wait(lock);
                h->wakeup = false;
#ifdef GOPLUS_DEBUG
                std::cerr << "normal wait done" << std::endl;
#endif
                return;
            }

            auto save = (*detail::get_detail().scheduler_current);
            detail::get_detail().scheduler_context->execute();
            *detail::get_detail().scheduler_current = (save);
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
