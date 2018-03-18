CoAwaitLib - co::await at any point of your code
===

A header-only stackfull coroutine library.
Make your own types awaitable, much like in the Coroutine TS. 
   
## Overview

```c++
#include <co/await.hpp>
#include <chrono>

using std::literals;

void poll()
{
   while(!done())
     co::await(25ms);
}
```

## Requirements

- C++14 (GCC 7.2, CLANG 4.0, MSVC 2015 or higher)
- [Boost](http://www.boost.org/) (context, coroutine2, asio and thread)
- A platform supported by [boost::context/boost::coroutine2](http://www.boost.org/doc/libs/1_66_0/libs/context/doc/html/context/requirements.html)
- Catch2 (ony for unit tests)
