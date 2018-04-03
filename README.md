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
        co::await(10ms); // sleep non-blocking
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

## Features

- co::Routine: A dispatchable and joinable coroutine handle with support for polimorphic allocators.
- co::Mutex: A mutex type that is compatible with std::unique_lock and suspends the current coroutine instead of blocking the thread.
- co::Routine::SpecificPtr: Provides coroutine local storage. Uses boost::thread_specific_ptr when called from outside of a coroutine.
- co::ip::tcp::socket: Wrappers around boost::asio networking types where the synchron functions like read_some and write_some are implemented by awaiting on the async version of these functions

## Requirements

- C++14 (GCC 7.2, CLANG 4.0, MSVC 2015 or higher)
- [Boost 1.66 or higher](http://www.boost.org/) (context, coroutine2, asio and thread)
- A platform supported by [boost::context/boost::coroutine2](http://www.boost.org/doc/libs/1_66_0/libs/context/doc/html/context/requirements.html)
- Catch2 (only for unit tests)

## Comparsion with C++20 stackless coroutines (Coroutine TS)

Stackless coroutines require compiler support.
The overhead of stackless coroutines is lower than the overhead of stackfull coroutines like used by this library (with regard to memory and cpu).
Stackless coroutines require duplications of interfaces that are required to be used synchron and asynchron.

Code that makes use of stackless coroutines has to be sprinkled with many uses of the co_await keyword while a single async operation with this library only requires one co::await() call deep inside of the callstack.
For instance, stackless coroutines require the specific range-for-co_await syntax for an async range, while this library could do with a regular range-for-loop and (optional) calls of co::await() inside of the begin() method and the  increment operator of the iterator.

## Comparsion with boost::fibre

In terms of developer experience, boost::fibre may be the closest match with this libray.
Both allow you to write code that looks synchron but does not block complete operating system threads.
Both are based on boost::context and will benefit from its futher development and fixes.
This library and boost::fibre are expected to be interchange able with a thin abstraction layer in many cases (TODO: verify).

- boost::fibre is used and test in a much broader range of use cases
- the performance of boost::fibre is expected to be higher for most use cases (TODO: actual benchmarks)
- the execution of fibres is based on schedulers while this library is (by default) starting the coroutine execution inline and carry on the execution on an boost::asio::io_context
- this library is expected to allow a smother integration existing application that could not be switched at once from blocking operations to fibres/coroutines
- while boost::fibre treats every thread as a fibre, this library distinguishes between regular threads (blocking behavior) and coroutines (non blocking behavior)

## API stability

This library is in an early development state.
APIs are likely to change, but the features are likely to stay.
