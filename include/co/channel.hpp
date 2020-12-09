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

   UnbufferedChannel(const UnbufferedChannel&) = delete;
   UnbufferedChannel operator=(const UnbufferedChannel&) = delete;

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

   [[nodiscard]] std::optional<T> pop()
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

/*
 * Multi producer + multi consumer channel.
 *
 * The last sender has to call closeSender() and the last receiver has to call closeReceiver().
 *
 * Receivers is blocked/suspended if the queue is empty.
 * Sender are blocked/suspended if the queue is full (default queue size is 2 - you can change it
 * with setMaxQueueSize()).
 *
 *
 */
template <typename T>
class BufferedChannel
{
 private:
   std::mutex mMutex;

   std::deque<T> mReadyData;

   using WaitingSender = impl::LightFutureData<bool>;
   std::deque<WaitingSender*> mWaitingSender;

   using WaitingReceiver = impl::LightFutureData<std::optional<T>>;
   std::deque<WaitingReceiver*> mWaitingReceiver;

   bool mSenderClosed{false};
   bool mReceiverClosed{false};
   size_t mMaxQueueSize{2};

   template <typename Queue>
   static auto pop(Queue& queue)
   {
      assert(!queue.empty());
      auto res = std::move(queue.front());
      queue.pop_front();
      return res;
   }

 public:
   using value_type = T;

   BufferedChannel() {}

   BufferedChannel(const BufferedChannel&) = delete;
   BufferedChannel operator=(const BufferedChannel&) = delete;

#ifdef NDEBUG
   ~BufferedChannel() = default;
#else
   ~BufferedChannel()
   {
      assert(mSenderClosed);
      assert(mReceiverClosed);
      assert(mWaitingSender.empty());
      assert(mWaitingReceiver.empty());
   }
#endif

   void setMaxQueueSize(size_t cnt)
   {
      std::deque<WaitingSender*> notify;

      {
         std::unique_lock lock{mMutex};
         assert(cnt > 0);

         if (mReadyData.size() < cnt)
         {
            for (int i = mMaxQueueSize; i < cnt; i++)
            {
               if (mWaitingSender.empty())
                  break;
               notify.push_back(pop(mWaitingSender));
            }
         }

         mMaxQueueSize = cnt;
      }

      for (auto& sender : notify)
         sender->set_value(false);
   }

   void closeSender()
   {
      std::deque<WaitingReceiver*> notify;

      {
         std::unique_lock lock{mMutex};
         assert(mSenderClosed == false);
         assert(mWaitingSender.empty());
         mSenderClosed = true;

         if (!mWaitingReceiver.empty())
         {
            assert(mReadyData.empty());
            notify = std::move(mWaitingReceiver);
            mWaitingReceiver.clear();
         }
      }

      for (auto& receiver : notify)
         receiver->set_value({});
   }

   [[nodiscard]] bool push(T val)
   {
      WaitingSender self{};
      WaitingReceiver* receiver{};

      {
         std::unique_lock lock{mMutex};
         assert(mSenderClosed == false);

         if (!mWaitingReceiver.empty())
         {
            assert(mReceiverClosed == false);
            receiver = pop(mWaitingReceiver);
            self.set_value(true);
         }
         else if (mReceiverClosed)
         {
            assert(mWaitingReceiver.empty());
            self.set_value(false);
         }
         else
         {
            mReadyData.push_back(std::move(val));
            if (mReadyData.size() >= mMaxQueueSize)
               mWaitingSender.push_back(&self);
            else
               self.set_value(true);
         }
      }

      if (receiver)
         receiver->set_value(std::move(val));

      return co::await(self);
   }

   void closeReceiver()
   {
      std::deque<WaitingSender*> notify;

      {
         std::unique_lock lock{mMutex};
         assert(mReceiverClosed == false);
         assert(mWaitingReceiver.empty());
         mReceiverClosed = true;
         notify = std::move(mWaitingSender);
         mWaitingSender.clear();
      }

      for (auto& sender : notify)
         sender->set_value(false);
   }

   [[nodiscard]] std::optional<T> pop()
   {
      WaitingReceiver self;
      WaitingSender* sender{};

      {
         std::unique_lock lock{mMutex};
         assert(mReceiverClosed == false);

         if (!mReadyData.empty())
         {
            self.set_value(pop(mReadyData));

            if (!mWaitingSender.empty())
               sender = pop(mWaitingSender);
         }
         else if (mSenderClosed)
         {
            assert(mWaitingSender.empty());
            self.set_value({});
         }
         else
            mWaitingReceiver.push_back(&self);
      }

      if (sender)
         sender->set_value(true);

      return co::await(self);
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

   struct sentinel
   {
   };

   class iterator
   {
    private:
      Receiver* mParent{nullptr};
      std::optional<value_type> mCurrent;

    public:
      explicit iterator(Receiver* parent) : mParent(parent), mCurrent(mParent->mChannel->pop()) {}

      bool operator==(sentinel) const { return !mCurrent; }

      bool operator!=(sentinel) const { return !!mCurrent; }

      const value_type& operator*() const { return *mCurrent; }
      value_type& operator*() { return *mCurrent; }

      iterator& operator++()
      {
         mCurrent = mParent->mChannel->pop();
         return *this;
      }
   };

   iterator begin() { return iterator{this}; }

   sentinel end() { return sentinel{}; }

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

template <typename T>
struct SharedChannel
{
   std::shared_ptr<Sender<std::shared_ptr<T>>> sender;
   std::shared_ptr<Receiver<std::shared_ptr<T>>> receiver;
};

template <typename T>
auto makeBufferedChannel(size_t maxQueueSize = 2)
{
   auto c = std::make_shared<BufferedChannel<T>>();
   c->setMaxQueueSize(maxQueueSize);

   using ChannelPtr = std::shared_ptr<BufferedChannel<T>>;
   return SharedChannel<BufferedChannel<T>>{std::make_shared<Sender<ChannelPtr>>(c),
                                            std::make_shared<Receiver<ChannelPtr>>(c)};
}

} // namespace co
