// runner file contents
#define NONIUS_RUNNER
#include "nonius.h++"

#include <boost/optional.hpp>
#include <boost/thread/future.hpp>

#include <co_future.hpp>

constexpr size_t innerLoopCnt = 1000;

template <typename T>
auto std_make_ready_future(T&& t)
{
   std::promise<std::decay_t<T>> p;
   auto f = p.get_future();
   p.set_value(std::forward<T>(t));
   return f;
}

NONIUS_BENCHMARK("StdFutureGetAndPostLoop", [](nonius::chronometer meter)
                 {
                    boost::asio::io_service ioService;
                    std::vector<std::thread> myThreads(2);

                    boost::optional<boost::asio::io_service::work> work{ioService};
                    for (auto&& t : myThreads)
                    {
                       t = std::thread{[&]
                                       {
                                          ioService.run();
                                       }};
                    }

                    auto lastFuture = std_make_ready_future(42);

                    meter.measure([&]
                                  {
                                     for (size_t i=0; i < innerLoopCnt; i++)
                                     {
                                     auto p = std::make_shared<std::promise<int>>();
                                     auto f = p->get_future();
                                     ioService.post([p = std::move(p)]
                                                    {
                                                       p->set_value(42);
                                                    });

                                     if (lastFuture.get() != 42)
                                        throw std::runtime_error("Invalid value");

                                     lastFuture = std::move(f);
                                     }
                                  });

                    if (lastFuture.get() != 42)
                       throw std::runtime_error("Invalid value");

                    work = boost::none;

                    for (auto&& t : myThreads)
                       t.join();
                 });

NONIUS_BENCHMARK("BoostFutureGetAndPostLoop", [](nonius::chronometer meter)
                 {
                    boost::asio::io_service ioService;
                    std::vector<std::thread> myThreads(2);

                    boost::optional<boost::asio::io_service::work> work{ioService};
                    for (auto&& t : myThreads)
                    {
                       t = std::thread{[&]
                                       {
                                          ioService.run();
                                       }};
                    }

                    auto lastFuture = boost::make_ready_future(42);

                    meter.measure([&]
                                  {
                                     for (size_t i=0; i < innerLoopCnt; i++)
                                     {
                                     auto p = std::make_shared<boost::promise<int>>();
                                     auto f = p->get_future();
                                     ioService.post([p = std::move(p)]
                                                    {
                                                       p->set_value(42);
                                                    });

                                     if (lastFuture.get() != 42)
                                        throw std::runtime_error("Invalid value");

                                     lastFuture = std::move(f);
                                     }
                                  });

                    if (lastFuture.get() != 42)
                       throw std::runtime_error("Invalid value");

                    work = boost::none;

                    for (auto&& t : myThreads)
                       t.join();
                 });

NONIUS_BENCHMARK("CoFutureGetAndPostLoop", [](nonius::chronometer meter)
                 {
                    boost::asio::io_service ioService;
                    std::vector<std::thread> myThreads(2);

                    boost::optional<boost::asio::io_service::work> work{ioService};
                    for (auto&& t : myThreads)
                    {
                       t = std::thread{[&]
                                       {
                                          ioService.run();
                                       }};
                    }

                    auto lastFuture = co::make_ready_future(42);

                    meter.measure([&]
                                  {
                                     for (size_t i=0; i < innerLoopCnt; i++)
                                     {
                                     auto f = co::async(ioService, []
                                                        {
                                                           return 42;
                                                        });

                                     if (lastFuture.get() != 42)
                                        throw std::runtime_error("Invalid value");

                                     lastFuture = std::move(f);
                                     }
                                  });

                    if (lastFuture.get() != 42)
                       throw std::runtime_error("Invalid value");

                    work = boost::none;

                    for (auto&& t : myThreads)
                       t.join();
                 });

NONIUS_BENCHMARK("CoFutureGet2AndPostLoop", [](nonius::chronometer meter)
                 {
                    boost::asio::io_service ioService;
                    std::vector<std::thread> myThreads(2);

                    boost::optional<boost::asio::io_service::work> work{ioService};
                    for (auto&& t : myThreads)
                    {
                       t = std::thread{[&]
                                       {
                                          ioService.run();
                                       }};
                    }

                    auto lastFuture = co::make_ready_future(42);

                    meter.measure([&]
                                  {
                                     for (size_t i=0; i < innerLoopCnt; i++)
                                     {
                                     auto f = co::async(ioService, []
                                                        {
                                                           return 42;
                                                        });

                                     if (lastFuture.get2() != 42)
                                        throw std::runtime_error("Invalid value");

                                     lastFuture = std::move(f);
                                     }
                                  });

                    if (lastFuture.get() != 42)
                       throw std::runtime_error("Invalid value");

                    work = boost::none;

                    for (auto&& t : myThreads)
                       t.join();
                 });

#if 0
NONIUS_BENCHMARK("StdFutureGetReadyFuture", [](nonius::chronometer meter)
                 {
                    meter.measure([&]
                                  {
                                     auto lastFuture = std_make_ready_future(42);
                                     if (lastFuture.get() != 42)
                                        throw std::runtime_error("Invalid value");
                                  });
                 });

NONIUS_BENCHMARK("BoostFutureGetReadyFuture", [](nonius::chronometer meter)
                 {
                    meter.measure([&]
                                  {
                                     auto lastFuture = boost::make_ready_future(42);
                                     if (lastFuture.get() != 42)
                                        throw std::runtime_error("Invalid value");
                                  });
                 });

NONIUS_BENCHMARK("CoFutureGetReadyFuture", [](nonius::chronometer meter)
                 {
                    meter.measure([&]
                                  {
                                     auto lastFuture = co::make_ready_future(42);
                                     if (lastFuture.get() != 42)
                                        throw std::runtime_error("Invalid value");
                                  });
                 });

NONIUS_BENCHMARK("CoFutureGet2ReadyFuture", [](nonius::chronometer meter)
                 {
                    meter.measure([&]
                                  {
                                     auto lastFuture = co::make_ready_future(42);
                                     if (lastFuture.get2() != 42)
                                        throw std::runtime_error("Invalid value");
                                  });
                 });

BENCHMARK(StdFutureGetAndPostLoop)->UseRealTime();
BENCHMARK(BoostFutureGetAndPostLoop)->UseRealTime();
BENCHMARK(CoFutureGetAndPostLoop)->UseRealTime();

BENCHMARK(StdFutureGetAndPostLoop)->UseRealTime()->ThreadPerCpu();
BENCHMARK(BoostFutureGetAndPostLoop)->UseRealTime()->ThreadPerCpu();
BENCHMARK(CoFutureGetAndPostLoop)->UseRealTime()->ThreadPerCpu();

BENCHMARK(StdFutureGetReadyFuture)->UseRealTime();
BENCHMARK(BoostFutureGetReadyFuture)->UseRealTime();
BENCHMARK(CoFutureGetReadyFuture)->UseRealTime();

BENCHMARK(StdFutureGetReadyFuture)->UseRealTime()->ThreadPerCpu();
BENCHMARK(BoostFutureGetReadyFuture)->UseRealTime()->ThreadPerCpu();
BENCHMARK(CoFutureGetReadyFuture)->UseRealTime()->ThreadPerCpu();

BENCHMARK_MAIN()
#endif
