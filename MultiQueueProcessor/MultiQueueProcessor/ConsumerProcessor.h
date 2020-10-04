#pragma once

#include <mutex>
#include <memory>
#include <future>

#include "IConsumer.h"
#include "IValueSource.h"

std::atomic_int32_t s_taskQueueSize = 0;

namespace MQP
{

/// <summary>
/// The class is responsible for one consumer notifying by means of tasks that are passed to a thread pool.
/// Also, the class controls that only one task is processed in the thread pool for the consumer at a time.
/// </summary>
template<typename Key, typename Value, typename TPool, typename Hash>
class ConsumerProcessor final : public IValueSourceConsumer<Key, Value>
                              , public std::enable_shared_from_this<ConsumerProcessor<Key, Value, TPool, Hash>>
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
      for (auto& [key, valueSource] : m_valueSources)
      {
         valueSource->Stop();
      }
   }

   ConsumerProcessor(const ConsumerProcessor&) = delete;
   ConsumerProcessor& operator=(const ConsumerProcessor&) = delete;
   ConsumerProcessor(ConsumerProcessor&&) = delete;
   ConsumerProcessor& operator=(ConsumerProcessor&&) = delete;

   /// <summary>
   /// Adds a value source to consumer processor
   /// </summary>
   /// <param name="key">A key for which a processed consumer needs data.</param>
   /// <param name="valueSource">A value source which provides data for the passed key.</param>
   void AddValueSource(const Key& key, IValueSourcePtr<Key, Value> valueSource)
   {
      std::scoped_lock lock(m_valueSourceMutex);

      if (std::end(m_valueSources) != m_valueSources.find(key))
      {
         return; // avoid a double subscription
      }

      m_valueSources.try_emplace(key, std::move(valueSource));
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

         valueSource = std::move(it->second);
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
   std::packaged_task<void()> createTask(IValueSourceWeakPtr<Key, Value> valueSource)
   {
      const auto s = m_valueSourceProcessingOrder.size();
      if (s > s_taskQueueSize)
      {
         s_taskQueueSize = s;
      }
      std::cout << int(s_taskQueueSize) << " - " << m_valueSourceProcessingOrder.size() << std::endl;

      return std::packaged_task<void()>([processor = weak_from_this(), valueSource = valueSource]()
      {
         auto spProcessor = processor.lock();
         if (!spProcessor)
         {
            return;
         }

         if (auto spValueSource = valueSource.lock())
         {
            if (!spValueSource->IsStopped() && spValueSource->HasValue())
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
      std::packaged_task<void()> nextTask;

      {
         std::scoped_lock lock(m_mutex);
         assert(m_state == EState::processing);

         while (!m_valueSourceProcessingOrder.empty())
         {
            auto nextValueSource = std::move(m_valueSourceProcessingOrder.front());
            m_valueSourceProcessingOrder.pop_front();

            auto valueSource = nextValueSource.lock();
            if (!valueSource || valueSource->IsStopped())
            {
               continue; // skip all stopped value sources
            }

            nextTask = createTask(std::move(nextValueSource));
            break;
         }

         if (!nextTask.valid() && m_valueSourceProcessingOrder.empty())
         {
            m_state = EState::free;
            return;
         }
      }


      m_threadPool->Post(std::move(nextTask), m_token);
   }

   /// <summary>
   /// A new value available in the value source event handler
   /// </summary>
   void OnNewValueAvailable(IValueSourcePtr<Key, Value> valueSource) override
   {
      std::packaged_task<void()> task;

      {
         std::scoped_lock lock(m_mutex);
         if (m_state == EState::processing)
         {
            m_valueSourceProcessingOrder.emplace_back(valueSource);
            return;
         }

         assert(m_state == EState::free);
         m_state = EState::processing;

         task = createTask(valueSource);
      }

      m_threadPool->Post(std::move(task), m_token);
   }

private:
   const IConsumerPtr<Key, Value> m_consumer;
   // a token is requiered in case a thread pool shall notify the consumer strictly from the same thread (STA simulation)
   const std::uintptr_t m_token; 
   std::mutex m_mutex; // guards m_state and m_valueSourceProcessingOrder
   EState m_state = EState::free;
   std::deque<IValueSourceWeakPtr<Key, Value>> m_valueSourceProcessingOrder; // keeps the calls order close to original
   mutable std::mutex m_valueSourceMutex; // guards m_valueSources
   std::unordered_map<Key, IValueSourcePtr<Key, Value>, Hash> m_valueSources;
   const std::shared_ptr<TPool> m_threadPool;
};

template<typename Key, typename Value, typename TPool, typename Hash>
using ConsumerProcessorPtr = std::shared_ptr<ConsumerProcessor<Key, Value, TPool, Hash>>;
}
