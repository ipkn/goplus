# goplus
goroutine extension for c++

 - Boost-Context based
 - Support similar grammar with go

```cpp
	using namespace goplus;

    auto ch = make_chan<int>();
    go+ [ch]() mutable{
        ch << 101;
    };

    go+ [ch]() mutable{
        int x;

        ch >> x;
	}
```

# Limitation

 - Basic implementation
 - main thread can't send to/recv from a channel (yet).

# Dependency

 - Boost 1.61.0
