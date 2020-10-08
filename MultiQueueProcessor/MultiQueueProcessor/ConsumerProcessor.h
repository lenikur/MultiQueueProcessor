#pragma once

#include <mutex>
#include <memory>
#include <future>
#include <deque>

#include "IConsumer.h"

namespace MQP
{

/// <summary>
/// The class is responsible for one consumer notifying by means of tasks that are passed to a thread pool.
/// Also, the class controls that only one task is processed in the thread pool for the consumer at a time.
/// </summary>
template<typename Key, typename Value, typename TPool, typename Hash>
class ConsumerProcessor : public std::enable_shared_from_this<ConsumerProcessor<Key, Value, TPool, Hash>>
{
   enum class EState { free, processing };
public:
   ConsumerProcessor(Key key, IConsumerPtr<Key, Value> consumer, std::shared_ptr<TPool> threadPool) 
      : m_consumer(std::move(consumer))
      , m_threadPool(std::move(threadPool))
      , m_key(std::move(key))
   {
   }

   ~ConsumerProcessor()
   {
   }

   ConsumerProcessor(const ConsumerProcessor&) = delete;
   ConsumerProcessor& operator=(const ConsumerProcessor&) = delete;
   ConsumerProcessor(ConsumerProcessor&&) = delete;
   ConsumerProcessor& operator=(ConsumerProcessor&&) = delete;

   /// <summary>
   /// A new value available in the passed value source event handler
   /// </summary>
   template <typename TValue>
   void Enqueue(TValue&& value)
   {
      {
         std::scoped_lock lock(m_mutex);
         if (m_state == EState::processing)
         {
            m_values.emplace_back(std::move(value));
            return;
         }

         assert(m_state == EState::free);
         m_state = EState::processing;
      }

      m_threadPool->Post(createTask(std::forward<TValue>(value)));
   }

private:
   
   using std::enable_shared_from_this<ConsumerProcessor<Key, Value, TPool, Hash>>::weak_from_this;

   void makeCall(Value&& value)
   {
      m_consumer->Consume(m_key, value);

      onValueProcessed();
   }

   /// <summary>
   /// Creates a consumer notification task for passing it to the thread pool.
   /// </summary>
   template <typename TValue>
   std::packaged_task<void()> createTask(TValue&& value)
   {
      return std::packaged_task<void()>([processor = weak_from_this(), value = std::forward<TValue>(value)]() mutable
      {
         if (auto spProcessor = processor.lock())
         {
            spProcessor->makeCall(std::move(value));
         }
      });
   }

   /// <summary>
   /// A consumer notification task completion handler.
   /// </summary>
   void onValueProcessed()
   {
      if (m_outValues.empty())
      {
         std::scoped_lock lock(m_mutex);

         if (m_values.empty())
         {
            assert(m_state == EState::processing);
            m_state = EState::free;
            return;
         }
         m_outValues.swap(m_values);
      }

      m_threadPool->Post(createTask(std::move(m_outValues.front())));
      m_outValues.pop_front();
   }

private:
   const IConsumerPtr<Key, Value> m_consumer;
   std::mutex m_mutex; // guards m_state and m_values
   EState m_state = EState::free;
   const Key m_key;
   std::deque<Value> m_values;
   std::deque<Value> m_outValues;
   const std::shared_ptr<TPool> m_threadPool;
};

template<typename Key, typename Value, typename TPool, typename Hash>
using ConsumerProcessorPtr = std::shared_ptr<ConsumerProcessor<Key, Value, TPool, Hash>>;
}
