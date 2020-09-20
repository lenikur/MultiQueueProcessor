#pragma once

#include <mutex>
#include <memory>
#include <future>

#include "IConsumer.h"
#include "IValueSource.h"
#include "ValueSourceGroup.h"

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
   ConsumerProcessor(IConsumerPtr<Key, Value> consumer, std::shared_ptr<TPool> threadPool) 
      : m_consumer(std::move(consumer))
      , m_token(reinterpret_cast<std::uintptr_t>(m_consumer.get()))
      , m_threadPool(std::move(threadPool))
   {
   }

   ~ConsumerProcessor()
   {
      m_valueSourceGroup->Stop();
   }

   ConsumerProcessor(const ConsumerProcessor&) = delete;
   ConsumerProcessor& operator=(const ConsumerProcessor&) = delete;
   ConsumerProcessor(ConsumerProcessor&&) = delete;
   ConsumerProcessor& operator=(ConsumerProcessor&&) = delete;

   void AddValueSource(const Key& key, IValueSourcePtr<Key, Value> valueSource)
   {
      m_valueSourceGroup->AddValueSource(key, std::move(valueSource));
   }

   void RemoveSubsciption(const Key& key)
   {
      m_valueSourceGroup->RemoveValueSource(key);
   }

   bool IsSubscribedToAny() const
   {
      m_valueSourceGroup->IsEmpty();
   }

   const IConsumerPtr<Key, Value>& GetConsumer()
   {
      return m_consumer;
   }

   const ValueSourceGroupPtr<Key, Value>& GetValueSource()
   {
      return m_valueSourceGroup;
   }

   void ConnectToValueSource()
   {
      m_valueSourceGroup->SetNewValueAvailableHandler([processor = weak_from_this()]()
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
            const auto& [key, value] = valueSource->GetValue();
            spProcessor->GetConsumer()->Consume(key, value);
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
      if (!m_valueSourceGroup->HasValue())
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
   std::mutex m_mutex; // guards m_state
   EState m_state = EState::free;
   ValueSourceGroupPtr<Key, Value> m_valueSourceGroup;
   const std::shared_ptr<TPool> m_threadPool;
};

template<typename Key, typename Value, typename TPool>
using ConsumerProcessorPtr = std::shared_ptr<ConsumerProcessor<Key, Value, TPool>>;
}
