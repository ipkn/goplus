#pragma once
#include <boost/context/all.hpp>

namespace goplus
{
    struct goroutine
    {
    public:
        goroutine() {}
        template <typename Func>
        goroutine(Func f)
        : ctx_(f)
        {
        }

        goroutine(goroutine&&) = default;
        goroutine& operator = (goroutine&& r) noexcept
        {
            ctx_ = std::move(r.ctx_);
            scheduler_ = r.scheduler_;
			r.scheduler_ = -1;
        }

        auto detach()
        {
            return std::move(ctx_());
        }

        void execute()
        {
            ctx_ = std::move(ctx_());
        }
        void set(boost::context::execution_context<void>&& ctx)
        {
            ctx_ = std::move(ctx);
        }
        void set_scheduler(int idx) noexcept
        {
            scheduler_ = idx;
        }
        int get_scheduler() noexcept
        {
            return scheduler_;
        }

        boost::context::execution_context<void> ctx_;
        int scheduler_{-1};
		std::atomic<bool> waked_from_select_{false};
    };

}
