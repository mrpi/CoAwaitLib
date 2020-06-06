#pragma once

#include "await.hpp"
#include "future.hpp"

namespace co
{

/*
 * Single producer + single consumer channel.
 *
 * The unbuffered channel acts as an rendezvous point:
 *  - the sender is blocked/suspended on push() till there is a receiver
 *  - the receiver is blocked on pop till there is a sender
 */
template <typename T>
class UnbufferedChannel
{
 private:
   std::optional<impl::LightFutureData<T*>> value;
   std::optional<impl::LightFutureData<bool>> valueRead;

 public:
   using value_type = T;

   UnbufferedChannel()
   {
      value.emplace();
      valueRead.emplace();
   }

   void closeSender() { value->set_value(nullptr); }

   [[nodiscard]] bool push(T val)
   {
      value->set_value(&val);
      auto res = co::await(*valueRead);
      valueRead.emplace();
      return res;
   }

   void closeReceiver()
   {
      co::await(*value);
      value.emplace();
      valueRead->set_value(false);
   }

   [[nodiscard]] boost::optional<T> pop()
   {
      if (auto ptr = co::await(*value))
      {
         auto res = std::move(*ptr);
         value.emplace();
         valueRead->set_value(true);
         return res;
      }
      else
         return {};
   }
};

template <typename ChannelPtr>
class Sender
{
 public:
   using value_type = typename std::decay_t<decltype(*std::declval<ChannelPtr>())>::value_type;

 private:
   ChannelPtr mChannel{nullptr};

 public:
   explicit Sender(ChannelPtr channel) : mChannel(std::move(channel)) {}

   Sender(const Sender&) = delete;
   Sender operator=(const Sender&) = delete;

   Sender(Sender&& other) : mChannel(std::move(other.mChannel)) { other.mChannel = nullptr; }
   Sender operator=(Sender&& other)
   {
      close();
      std::swap(mChannel, other.mChannel);
   }

   ~Sender() { close(); }

   [[nodiscard]] bool operator()(value_type val) { return mChannel->push(std::move(val)); }

   void close()
   {
      if (!mChannel)
         return;

      mChannel->closeSender();
      mChannel = nullptr;
   }
};

template <typename ChannelPtr>
class Receiver
{
 public:
   using value_type = typename std::decay_t<decltype(*std::declval<ChannelPtr>())>::value_type;

 private:
   ChannelPtr mChannel{nullptr};

 public:
   explicit Receiver(ChannelPtr channel) : mChannel(std::move(channel)) {}

   Receiver(const Receiver&) = delete;
   Receiver operator=(const Receiver&) = delete;

   Receiver(Receiver&& other) : mChannel(std::move(other.mChannel)) { other.mChannel = nullptr; }
   Receiver operator=(Receiver&& other)
   {
      close();
      std::swap(mChannel, other.mChannel);
   }

   ~Receiver() { close(); }

   class iterator
   {
    private:
      Receiver* mParent{nullptr};
      boost::optional<value_type> mCurrent;

    public:
      iterator() = default;
      iterator(Receiver* parent) : mParent(parent), mCurrent(mParent->mChannel->pop()) {}

      bool operator==(iterator other) const
      {
         if (mParent == other.mParent)
            return true;

         if (!mParent)
            return !other.mCurrent;

         if (!other.mParent)
            return !mCurrent;

         return false;
      }

      bool operator!=(iterator other) const { return !(*this == other); }

      const value_type& operator*() const { return *mCurrent; }
      value_type& operator*() { return *mCurrent; }

      iterator& operator++()
      {
         mCurrent = mParent->mChannel->pop();
         return *this;
      }
   };

   iterator begin() { return iterator{this}; }

   iterator end() { return iterator{}; }

   void close()
   {
      if (!mChannel)
         return;

      mChannel->closeReceiver();
      mChannel = nullptr;
   }
};

template <typename T>
struct Channel
{
   Sender<std::shared_ptr<T>> sender;
   Receiver<std::shared_ptr<T>> receiver;
};

/**
 * Creates a Sender and a Receiver that share ownership of an UnbufferedChannel
 */
template <typename T>
Channel<UnbufferedChannel<T>> makeUnbufferedChannel()
{
   auto c = std::make_shared<UnbufferedChannel<T>>();
   return {Sender{c}, Receiver{c}};
}

} // namespace co
