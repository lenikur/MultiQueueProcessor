#pragma once

#include <mutex>
#include <memory>
#include <future>

#include "IConsumer.h"
#include "IValueSource.h"

namespace MQP
{
/// <summary>
/// The class is responsible for one consumer notifying by means of tasks that are passed to a thread pool.
/// Also, the class controls that only one task is processed in the thread pool for the consumer at a time.
/// </summary>
template<typename Key, typename Value, typename TPool>
class ConsumerProcessor final : public std::enable_shared_from_this<ConsumerProcessor<Key, Value, TPool>>
{
   enum class EState { free, processing };
public:
   ConsumerProcessor(Key key, IConsumerPtr<Key, Value> consumer, IValueSourcePtr<Value> valueSource, std::shared_ptr<TPool> threadPool) 
      : m_consumer(std::move(consumer))
      , m_token(reinterpret_cast<std::uintptr_t>(m_consumer.get()))
      , m_key(std::move(key))
      , m_threadPool(std::move(threadPool))
      , m_valueSource(std::move(valueSource))
   {
   }

   ~ConsumerProcessor()
   {
      m_valueSource->Stop();
   }

   ConsumerProcessor(const ConsumerProcessor&) = delete;
   ConsumerProcessor& operator=(const ConsumerProcessor&) = delete;
   ConsumerProcessor(ConsumerProcessor&&) = delete;
   ConsumerProcessor& operator=(ConsumerProcessor&&) = delete;

   const Key& GetKey() const
   {
      return m_key;
   }

   const IConsumerPtr<Key, Value>& GetConsumer()
   {
      return m_consumer;
   }

   const IValueSourcePtr<Value>& GetValueSource()
   {
      return m_valueSource;
   }

   void ConnectToValueSource()
   {
      m_valueSource->SetNewValueAvailableHandler([processor = weak_from_this()]()
      {
         if (auto spProcessor = processor.lock())
         {
            spProcessor->onNewValueAvailable();
         }
      });
   }

   using std::enable_shared_from_this<ConsumerProcessor<Key, Value, TPool>>::weak_from_this;

private:

   /// <summary>
   /// Creates a task for passing it to the thread pool.
   /// </summary>
   std::packaged_task<void()> createTask()
   {
      return std::packaged_task<void()>([processor = weak_from_this()]()
      {
         if (auto spProcessor = processor.lock())
         {
            auto& valueSource = spProcessor->GetValueSource();
            assert(valueSource->HasValue());
            spProcessor->GetConsumer()->Consume(spProcessor->GetKey(), valueSource->GetValue());
            valueSource->MoveNext();
            spProcessor->onValueProcessed();
         }
      });
   }

   /// <summary>
   /// The task completion handler.
   /// </summary>
   void onValueProcessed()
   {
      if (!m_valueSource->HasValue())
      {
         std::scoped_lock lock(m_mutex);
         m_state = EState::free;
         return;
      }

      m_threadPool->Post(createTask(), m_token);
   }

   /// <summary>
   /// A new value available in the value source event handler
   /// </summary>
   void onNewValueAvailable()
   {
      {
         std::scoped_lock lock(m_mutex);
         if (m_state == EState::processing)
         {
            return;
         }

         m_state = EState::processing;
      }

      m_threadPool->Post(createTask(), m_token);
   }

private:
   const IConsumerPtr<Key, Value> m_consumer;
   // a token is requiered in case a thread pool shall notify the consumer strictly from the same thread (STA simulation)
   const std::uintptr_t m_token; 
   const Key m_key;
   std::mutex m_mutex; // guards m_state
   EState m_state = EState::free;
   const IValueSourcePtr<Value> m_valueSource;
   const std::shared_ptr<TPool> m_threadPool;
};

template<typename Key, typename Value, typename TPool>
using ConsumerProcessorPtr = std::shared_ptr<ConsumerProcessor<Key, Value, TPool>>;
}
