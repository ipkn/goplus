#pragma once

#include <memory>
#include <mutex>
#include "scheduler.h"
#include "goroutine.h"

namespace goplus
{
    template <typename T>
    class chan_internal
	{
    public:
        chan_internal(int)
        {
        }

		bool get(T& x)
		{
            lock.lock();
            if (!senders.empty())
            {
                auto sender = senders.front();
                senders.pop_front();
                x = sender.second;
                lock.unlock();
                scheduler::resume(*sender.first);
                return true;
            }
            recvers.emplace_back(scheduler::current(), &x);
            lock.unlock();
            scheduler::park();
            return true;
		}
		bool put(const T& x)
		{
            lock.lock();
            if (!recvers.empty())
            {
                auto recver = recvers.front();
                recvers.pop_front();
                *recver.second = x;
                lock.unlock();
                scheduler::resume(*recver.first);
                return true;
            }
            senders.emplace_back(scheduler::current(), x);
            lock.unlock();
            scheduler::park();
            return true;
		}

        std::mutex lock;
        std::deque<std::pair<goroutine*,T>> senders;
        std::deque<std::pair<goroutine*,T*>> recvers;
	};

    template <typename T>
    class chan
    {
    public:
        chan(int buffer_size)
			: self_(new chan_internal<T>(buffer_size))
        {
        }

        std::shared_ptr<chan_internal<T>> self_;

		template <typename U>
		friend
		chan<T>& operator >> (chan<U>& ch, T& x)
		{
			ch.self_->get(x);
			return ch;
		}

		template <typename U>
		friend
		chan<T>& operator << (chan<U>& ch, const T& x)
		{
			ch.self_->put(x);
			return ch;
		}

    };

    template <typename T>
    inline chan<T> make_chan(int buffer_size = 0)
    {
        return chan<T>(buffer_size);
    };

    //struct SinkDummy
    //{
    //};

    //extern SinkDummy _;
}

