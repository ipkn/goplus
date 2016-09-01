#pragma once

namespace goplus
{
    struct GoDummy
    {
    };

    extern GoDummy go;

    template <typename Func>
    inline void operator + (GoDummy, Func f)
    {
        static_assert(false, "Compile with goplus first.");
        f();
    }
}
