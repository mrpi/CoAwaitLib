CoAwaitLib - co::await at any point of your code
===

Write highly parallel and non-blocking code that looks and feels nearly like simple synchron code.

A header-only stackfull coroutine library.
Make your own types awaitable, much like in the Coroutine TS. 

## Example

```c++
#include <co/await.hpp>

using namespace std::literals;

bool done(int idx)
{
    return rand() % 32 == idx;
}

void poll(int idx)
{
    while (!done(idx))
        co::await(10ms);
    std::cout << "Found " << idx << std::endl;
}

int main(int argc, char* argv[])
{
    if (argc >= 2 && argv[1] == "async"s) {
        boost::asio::io_context context;
        co::Routine{context, []{poll(0);}} .detach();
        co::Routine{context, []{poll(1);}} .detach();
        context.run();
    } else {
        poll(0);
        poll(1);
    }
}
```

## Overview

co::await() in stackfull coroutines allows you to make your code run asynchron without changing every interface to return a future<T>/task<T> that may interally call asynchron code.

co::await() is non-blocking when it's running inside of a coroutine and blocking when it's running outside of a coroutine. This allows a smooth migration.

## Requirements

- C++14 (GCC 7.2, CLANG 4.0, MSVC 2015 or higher)
- [Boost](http://www.boost.org/) (context, coroutine2, asio and thread)
- A platform supported by [boost::context/boost::coroutine2](http://www.boost.org/doc/libs/1_66_0/libs/context/doc/html/context/requirements.html)
- Catch2 (ony for unit tests)
