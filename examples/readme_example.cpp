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
