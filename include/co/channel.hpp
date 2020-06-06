#pragma once

#include "await.hpp"
#include "future.hpp"

namespace co
{
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

template <typename Channel>
class Sender
{
 private:
   Channel* mChannel{nullptr};

 public:
   explicit Sender(Channel& channel) : mChannel(&channel) {}

   Sender(const Sender&) = delete;
   Sender operator=(const Sender&) = delete;

   Sender(Sender&& other) : mChannel(other.mChannel) { other.mChannel = nullptr; }
   Sender operator=(Sender&& other)
   {
      close();
      std::swap(mChannel, other.mChannel);
   }

   ~Sender() { close(); }

   [[nodiscard]] bool operator()(typename Channel::value_type val)
   {
      return mChannel->push(std::move(val));
   }

   void close()
   {
      if (!mChannel)
         return;

      mChannel->closeSender();
      mChannel = nullptr;
   }
};

template <typename Channel>
class Receiver
{
 private:
   Channel* mChannel{nullptr};
   boost::optional<typename Channel::value_type> mCurrent{mChannel->pop()};

 public:
   explicit Receiver(Channel& channel) : mChannel(&channel) {}

   Receiver(const Receiver&) = delete;
   Receiver operator=(const Receiver&) = delete;

   Receiver(Receiver&& other) : mChannel(other.mChannel) { other.mChannel = nullptr; }
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

    public:
      iterator() = default;
      iterator(Receiver* parent) : mParent(parent) {}

      bool operator==(iterator other) const
      {
         if (mParent == other.mParent)
            return true;

         if (!mParent)
            return !other.mParent->mCurrent;

         if (!other.mParent)
            return !mParent->mCurrent;

         return false;
      }

      bool operator!=(iterator other) const { return !(*this == other); }

      typename Channel::value_type& operator*() const { return *mParent->mCurrent; }

      iterator& operator++()
      {
         mParent->mCurrent = mParent->mChannel->pop();
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
} // namespace co
