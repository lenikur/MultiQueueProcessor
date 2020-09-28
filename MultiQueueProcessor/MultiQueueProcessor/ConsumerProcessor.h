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
template<typename Key, typename Value, typename TPool, typename Hash>
class ConsumerProcessor final : public std::enable_shared_from_this<ConsumerProcessor<Key, Value, TPool, Hash>>
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

   void AddValueSource(const Key& key, IValueSourcePtr<Value> valueSource)
   {
      auto& [itValueSource, isAdded] = m_valueSources.emplace(key, std::move(valueSource));
      if (isAdded)
      {
         itValueSource->second->SetNewValueAvailableHandler([processor = weak_from_this(), valueSource = std::weak_ptr<>(valueSource)]()
         {
            if (auto spProcessor = processor.lock() && auto spValueSource = valueSource.lock())
            {
               spProcessor->onNewValueAvailable();
            }
         });
      }
   }

   void RemoveSubscription(const Key& key)
   {
      m_valueSources.erase(key);
   }

   bool IsSubscribedToAny() const
   {
      return !m_valueSources.empty();
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
   std::packaged_task<void()> createTask(Key key, IValueSourceWeakPtr<Value> valueSource)
   {
      return std::packaged_task<void()>([processor = weak_from_this(), key = std::move(key), valueSource = IValueSourceWeakPtr<Value>(valueSource)]()
      {
         auto spProcessor = = processor.lock(); // TODO: do we really need it?? check task and ConsumerProcessor life time

         if (!spProcessor)
         {
            return;
         }

         if (auto spValueSource = valueSource.lock())
         {
            if (valueSource->HasValue()) // TODO: really need this check?
            {
               spProcessor->GetConsumer()->Consume(key, spValueSource->GetValue());
               valueSource->MoveNext();
            }
         }

         spProcessor->onValueProcessed();
      });
   }

   /// <summary>
   /// The task completion handler.
   /// </summary>
   void onValueProcessed()
   {
      std::scoped_lock lock(m_mutex);

      if (m_tasks.empty())
      {
         return;
      }

      m_threadPool->Post(std::move(m_tasks.front()), m_token);
      m_tasks.pop_front();
   }

   /// <summary>
   /// A new value available in the value source event handler
   /// </summary>
   void onNewValueAvailable(const Key& key, IValueSourcePtr<Value> valueSource)
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
   using ValueSources = std::unordered_map<Key, IValueSourcePtr<Value>, Hash>;
   ValueSources m_valueSources;
   std::deque<std::packaged_task<void()>> m_tasks;
   const std::shared_ptr<TPool> m_threadPool;
};

template<typename Key, typename Value, typename TPool, typename Hash>
using ConsumerProcessorPtr = std::shared_ptr<ConsumerProcessor<Key, Value, TPool, Hash>>;
}
