#pragma once

#include <memory>
#include <mutex>
#include "scheduler.h"
#include "goroutine.h"

namespace goplus
{
    struct channel_closed_exception
    {
    };

    template <typename T>
    class chan_internal
	{
    public:
        chan_internal(int buffer_size)
            :buffer_size_(buffer_size)
        {
        }

		void get(T& x, bool* ok = nullptr)
		{
#ifdef GOPLUS_DEBUG
            std::cerr << "get begin" << std::endl;
#endif
            lock_.lock();
            if (closed_)
            {
                x = T();
                if (ok)
                    *ok = false;
                lock_.unlock();
                return;
            }

#ifdef GOPLUS_DEBUG
            std::cerr << "get sender size " << senders_.size() << std::endl;
#endif
            while (!senders_.empty())
            {
#ifdef GOPLUS_DEBUG
            std::cerr << "get search sender" << std::endl;
#endif
                auto sender = senders_.front();
                senders_.pop_front();
                bool expected = false;
                auto g = std::get<0>(sender);
                if (g->waked_from_select_ == true &&
                        !g->waked_from_select_.compare_exchange_strong(expected, true))
                {
#ifdef GOPLUS_DEBUG
            std::cerr << "get!!" << std::endl;
#endif
                    // waked by another goroutine
                    continue;
                }

                if (!stock_.empty())
                {
#ifdef GOPLUS_DEBUG
            std::cerr << "get from sender && q" << std::endl;
#endif
                    x = stock_.front();
                    stock_.pop_front();

                    stock_.push_back(*std::get<1>(sender));
                    if (std::get<2>(sender))
                        *std::get<2>(sender) = true;
                }
                else
                {
#ifdef GOPLUS_DEBUG
            std::cerr << "get from sender" << std::endl;
#endif
                    x = *std::get<1>(sender);
                    if (std::get<2>(sender))
                        *std::get<2>(sender) = true;
                }
                if (ok)
                    *ok = true;
                lock_.unlock();
                scheduler::resume(*std::get<0>(sender));
#ifdef GOPLUS_DEBUG
            std::cerr << "get fin" << std::endl;
#endif
                return;
            }
            
            if (!stock_.empty())
            {
#ifdef GOPLUS_DEBUG
                std::cerr << "get from q" << std::endl;
#endif
                x = stock_.front();
                stock_.pop_front();
                if (ok)
                    *ok = true;
                lock_.unlock();
                return;
            }
#ifdef GOPLUS_DEBUG
            std::cerr << "get sleep" << std::endl;
#endif
            recvers_.emplace_back(scheduler::current(), &x, ok);
            lock_.unlock();
            scheduler::park();
#ifdef GOPLUS_DEBUG
            std::cerr << "get wake" << std::endl;
#endif
		}

		void put(const T& x, bool* ok = nullptr)
		{
#ifdef GOPLUS_DEBUG
            std::cerr << "put begin" << std::endl;
#endif
            lock_.lock();
#ifdef GOPLUS_DEBUG
            std::cerr << "put recver size " << recvers_.size() << std::endl;
#endif
            while (!recvers_.empty())
            {
#ifdef GOPLUS_DEBUG
                std::cerr << "put search recver" << std::endl;
#endif
                auto recver = recvers_.front();
                recvers_.pop_front();
                bool expected = false;
                auto g = std::get<0>(recver);
                if (g->waked_from_select_ == true &&
                        !g->waked_from_select_.compare_exchange_strong(expected, true))
                {
                    // waked by another goroutine
#ifdef GOPLUS_DEBUG
            std::cerr << "put!!" << std::endl;
#endif
                    continue;
                }

#ifdef GOPLUS_DEBUG
                std::cerr << "put found one" << std::endl;
#endif
                *std::get<1>(recver) = x;
                if (std::get<2>(recver))
                    *std::get<2>(recver) = true;
                lock_.unlock();
                scheduler::resume(*std::get<0>(recver));
                if (ok)
                    *ok = true;
#ifdef GOPLUS_DEBUG
                std::cerr << "put fin" << std::endl;
#endif
                return;
            }
#ifdef GOPLUS_DEBUG
            std::cerr << "put check q" << std::endl;
#endif
            if (stock_.size() < buffer_size_)
            {
                stock_.push_back(x);
                if (ok)
                    *ok = true;
#ifdef GOPLUS_DEBUG
                std::cerr << "put to q" << std::endl;
#endif
                lock_.unlock();
                return;
            }

#ifdef GOPLUS_DEBUG
            std::cerr << "put sleep" << std::endl;
#endif
            senders_.emplace_back(scheduler::current(), &x, ok);
            lock_.unlock();
            scheduler::park();
#ifdef GOPLUS_DEBUG
            std::cerr << "put wake" << std::endl;
#endif
		}

        void close()
        {
            lock_.lock();
            closed_ = true;
            while (!recvers_.empty())
            {
                auto recver = recvers_.front();
                recvers_.pop_front();
                bool expected = false;
                auto g = std::get<0>(recver);
                if (g->waked_from_select_ == true &&
                        !g->waked_from_select_.compare_exchange_strong(expected, true))
                {
                    // waked by another goroutine
                    continue;
                }

                *std::get<1>(recver) = T();
                if (std::get<2>(recver))
                    *std::get<2>(recver) = false;
                scheduler::resume(*g);
            }
            while (!senders_.empty())
            {
                auto sender = senders_.front();
                senders_.pop_front();
                bool expected = false;
                auto g = std::get<0>(sender);
                if (g->waked_from_select_ == true &&
                        !g->waked_from_select_.compare_exchange_strong(expected, true))
                {
                    // waked by another goroutine
                    continue;
                }
                if (std::get<2>(sender))
                    *std::get<2>(sender) = false;
                else
                {
                    //scheduler::exception(*sender.first, channel_closed_exception());
                }
            }
            lock_.unlock();
        }

    private:
        std::mutex lock_;
        std::deque<std::tuple<goroutine*,const T*, bool*>> senders_;
        std::deque<std::tuple<goroutine*,T*, bool*>> recvers_;
        std::deque<T> stock_;
        bool closed_{false};
        std::atomic<bool> waked_from_select_;
        int buffer_size_{0};
	};

    template <typename T>
    class chan
    {
    public:
        chan()
        {
        }

        chan(int buffer_size)
			: self_(new chan_internal<T>(buffer_size))
        {
        }

        std::shared_ptr<chan_internal<T>> self_;

        void reset()
        {
            self_.reset();
        }

        void close()
        {
            if (self_)
                self_->close();
        }

        friend bool operator != (const chan<T>& l, const chan<T>& r)
        {
            return l.self_ != r.self_;
        }

        friend bool operator == (const chan<T>& l, const chan<T>& r)
        {
            return l.self_ == r.self_;
        }

		friend chan<T>& operator >> (chan<T>& ch, T& x)
		{
            if (!ch.self_)
            {
                scheduler::park();
            }
			ch.self_->get(x);
			return ch;
		}

		friend chan<T>& operator >> (chan<T>& ch, const std::tuple<T&, bool&>& x)
		{
            if (!ch.self_)
            {
                scheduler::park();
            }
			ch.self_->get(std::get<0>(x), &std::get<1>(x));
			return ch;
		}

		friend chan<T>& operator << (chan<T>& ch, const T& x)
		{
            if (!ch.self_)
            {
                scheduler::park();
            }
			ch.self_->put(x);
			return ch;
		}

		friend chan<T>& operator << (chan<T>& ch, const std::tuple<const T&,bool&>& x)
		{
            if (!ch.self_)
            {
                scheduler::park();
            }
			ch.self_->put(std::get<0>(x), &std::get<1>(x));
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

