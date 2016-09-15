# goplus
goroutine extension for c++

 - Boost-Context based
 - Support similar grammar with go

```cpp
	using namespace goplus;

    auto ch = make_chan<int>();
    go+ [&ch]() {
        ch << 101;
    };

    go+ [&ch]() {
        int x;

        ch >> x;
	}
```

# Limitation

 - Basic implementation (no select yet)

# Dependency

 - Boost 1.61.0
