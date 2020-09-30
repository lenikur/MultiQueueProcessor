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
      std::scoped_lock lock(m_valueSourceMutex);
      // TODO: Do we really need it?
      for (auto& [key, valueSource] : m_valueSources)
      {
         valueSource->Stop();
      }
   }

   ConsumerProcessor(const ConsumerProcessor&) = delete;
   ConsumerProcessor& operator=(const ConsumerProcessor&) = delete;
   ConsumerProcessor(ConsumerProcessor&&) = delete;
   ConsumerProcessor& operator=(ConsumerProcessor&&) = delete;

   void AddValueSource(const Key& key, IValueSourcePtr<Key, Value> valueSource) // TODO: remove key argument as valueSource does have it
   {
      std::scoped_lock lock(m_valueSourceMutex);

      const auto& [itValueSource, isAdded] = m_valueSources.emplace(key, std::move(valueSource));

      if (isAdded) // TODO: out of lock?
      {
         itValueSource->second->SetNewValueAvailableHandler([processor = weak_from_this()](IValueSourcePtr<Key, Value> valueSource)
         {
            if (auto spProcessor = processor.lock()) // TODO: think about processor as shared_ptr (performance reason)
            {
               spProcessor->onNewValueAvailable(valueSource);
            }
         });
      }
   }

   void RemoveSubscription(const Key& key)
   {
      IValueSourcePtr<Key, Value> valueSource;

      {
         std::scoped_lock lock(m_valueSourceMutex);

         auto it = m_valueSources.find(key);
         if (it == std::end(m_valueSources))
         {
            return;
         }

         valueSource = it->second; // TODO: std::move???

         m_valueSources.erase(it);
      }

      valueSource->Stop();
   }

   bool IsSubscribedToAny() const
   {
      std::scoped_lock lock(m_valueSourceMutex);

      return !m_valueSources.empty();
   }

   const IConsumerPtr<Key, Value>& GetConsumer() const
   {
      return m_consumer;
   }

private:

   using std::enable_shared_from_this<ConsumerProcessor<Key, Value, TPool, Hash>>::weak_from_this;

   /// <summary>
   /// Creates a task for passing it to the thread pool.
   /// </summary>
   std::packaged_task<void()> createTask(IValueSourcePtr<Key, Value> valueSource)
   {
      return std::packaged_task<void()>([processor = weak_from_this(), valueSource = IValueSourceWeakPtr<Key, Value>(valueSource)]()
      {
         auto spProcessor = processor.lock(); // TODO: think about lock.. shared_ptr (better performance)?
         if (!spProcessor)
         {
            return;
         }

         if (auto spValueSource = valueSource.lock()) // TODO: think about CancelationToken and shared_ptr. it allows avoiding .lock() (better performance)
         {
            if (spValueSource->HasValue()) // TODO: really need this check?
            {
               const auto& [key, value] = spValueSource->GetValue();
               spProcessor->GetConsumer()->Consume(key, value);
               spValueSource->MoveNext();
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
      std::packaged_task<void()> task;

      {
         std::scoped_lock lock(m_mutex);
         assert(m_state == EState::processing);

         if (m_tasks.empty())
         {
            m_state = EState::free;
            return;
         }
         task = std::move(m_tasks.front());
         m_tasks.pop_front();
      }


      m_threadPool->Post(std::move(task), m_token);
   }

   /// <summary>
   /// A new value available in the value source event handler
   /// </summary>
   void onNewValueAvailable(IValueSourcePtr<Key, Value> valueSource)
   {
      auto task = createTask(std::move(valueSource));

      {
         std::scoped_lock lock(m_mutex);
         if (m_state == EState::processing)
         {
            m_tasks.emplace_back(std::move(task));
            return;
         }

         assert(m_state == EState::free);
         m_state = EState::processing;
      }

      m_threadPool->Post(std::move(task), m_token);
   }

private:
   const IConsumerPtr<Key, Value> m_consumer;
   // a token is requiered in case a thread pool shall notify the consumer strictly from the same thread (STA simulation)
   const std::uintptr_t m_token; 
   std::mutex m_mutex; // guards m_state and m_tasks
   EState m_state = EState::free;
   std::mutex m_valueSourceMutex; // guards m_valueSources
   using ValueSources = std::unordered_map<Key, IValueSourcePtr<Key, Value>, Hash>;
   ValueSources m_valueSources;
   std::deque<std::packaged_task<void()>> m_tasks;
   const std::shared_ptr<TPool> m_threadPool;
};

template<typename Key, typename Value, typename TPool, typename Hash>
using ConsumerProcessorPtr = std::shared_ptr<ConsumerProcessor<Key, Value, TPool, Hash>>;
}
