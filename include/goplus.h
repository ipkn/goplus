#pragma once

// -1: number of processor
#define GOPLUS_CONCURRENCY -1

#include "channel.h"
#include "scheduler.h"

namespace goplus
{
    struct GoDummy
    {
    };

    extern GoDummy go;

    template <typename Func>
    inline void operator + (GoDummy, Func f)
    {
        scheduler::spawn(f);
    }
}
