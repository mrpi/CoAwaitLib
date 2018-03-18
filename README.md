CoAwaitLib - co::await at any point of your code
===

A header-only stackfull coroutine library.
Make your own types awaitable, much like the Coroutine TS. 
   
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

- C++14
- [Boost](http://www.boost.org/)
- A platform supported by [boost::context/boost::coroutine2](http://www.boost.org/doc/libs/1_66_0/libs/context/doc/html/context/requirements.html)
