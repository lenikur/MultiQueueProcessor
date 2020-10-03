#pragma once

#include <mutex>
#include <memory>
#include <future>

#include "IConsumer.h"
#include "IValueSource.h"

std::atomic_int32_t s_taskQueueSize = 0;

namespace MQP
{

class CancellationTokenSource;

class CancellationToken
{
   friend CancellationTokenSource;
   explicit CancellationToken(std::weak_ptr<bool> isCancellationRequested)
      : m_isCancellationRequested(std::move(isCancellationRequested))
   {
   }

public:

   CancellationToken(const CancellationToken&) = default;
   CancellationToken& operator=(const CancellationToken&) = default;
   CancellationToken(CancellationToken&&) = default;
   CancellationToken& operator=(CancellationToken&&) = default;

   bool IsCancellationRequested() const noexcept
   {
      return m_isCancellationRequested.expired();
   }

private:
   std::weak_ptr<bool> m_isCancellationRequested;
};

class CancellationTokenSource
{
public:
   bool IsCancellationRequested() const
   {
      return *m_isCancellationRequested;
   }

   void Cancel()
   {
      *m_isCancellationRequested = true;
   }

   CancellationToken GetToken()
   {
      return CancellationToken(m_isCancellationRequested);
   }
   
private:
   std::shared_ptr<bool> m_isCancellationRequested = std::make_shared<bool>(false);
};

/// <summary>
/// The class is responsible for one consumer notifying by means of tasks that are passed to a thread pool.
/// Also, the class controls that only one task is processed in the thread pool for the consumer at a time.
/// </summary>
template<typename Key, typename Value, typename TPool, typename Hash>
class ConsumerProcessor final : public std::enable_shared_from_this<ConsumerProcessor<Key, Value, TPool, Hash>>
{
   enum class EState { free, processing };
   enum ETask {task, cancellationToken};
   enum EValueSource {valueSource, cancellationTokenSource};
public:
   ConsumerProcessor(IConsumerPtr<Key, Value> consumer, std::shared_ptr<TPool> threadPool) 
      : m_consumer(std::move(consumer))
      , m_id(reinterpret_cast<std::uintptr_t>(m_consumer.get()))
      , m_threadPool(std::move(threadPool))
   {
   }

   ~ConsumerProcessor()
   {
      std::scoped_lock lock(m_valueSourceMutex);
      // TODO: Do we really need it?
      for (auto& [key, valueSource] : m_valueSources)
      {
         std::get<EValueSource::cancellationTokenSource>(valueSource).Cancel();
         std::get<EValueSource::valueSource>(valueSource)->Stop();
      }
   }

   ConsumerProcessor(const ConsumerProcessor&) = delete;
   ConsumerProcessor& operator=(const ConsumerProcessor&) = delete;
   ConsumerProcessor(ConsumerProcessor&&) = delete;
   ConsumerProcessor& operator=(ConsumerProcessor&&) = delete;

   void AddValueSource(const Key& key, IValueSourcePtr<Key, Value> valueSource) // TODO: remove key argument as valueSource does have it
   {
      std::scoped_lock lock(m_valueSourceMutex);

      const auto& [itValueSource, isAdded] = m_valueSources.try_emplace(key, std::move(valueSource), CancellationTokenSource{});

      if (isAdded) // TODO: out of lock?
      {
         auto token = std::get<EValueSource::cancellationTokenSource>(itValueSource->second).GetToken();
         std::get<EValueSource::valueSource>(itValueSource->second)->SetNewValueAvailableHandler(
            [processor = weak_from_this(), token = std::move(token)](IValueSourcePtr<Key, Value> valueSource)
         {
            if (auto spProcessor = processor.lock()) // TODO: think about processor as shared_ptr (performance reason)
            {
               spProcessor->onNewValueAvailable(valueSource, token);
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

         //C<decltype(it)> f;

         auto t = it->second;
         //C<decltype(t)> f;

         valueSource = std::get<EValueSource::valueSource>(t); // TODO: std::move???

         m_valueSources.erase(it); // CancellationTokenSource makes cancellation in dtor
      }

      valueSource->Stop();
   }

   bool IsSubscribedToAny() const // TODO: need it?
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
   std::packaged_task<void()> createTask(IValueSourcePtr<Key, Value> valueSource, CancellationToken token)
   {
      const auto& [key, value] = valueSource->GetValue();
      const auto s = m_tasks.size();
      if (s > s_taskQueueSize)
      {
         s_taskQueueSize = s;
      }
      std::cout << int(s_taskQueueSize) << " - " << m_tasks.size() << std::endl;

      return std::packaged_task<void()>([processor = weak_from_this(), valueSource = IValueSourceWeakPtr<Key, Value>(valueSource), token = std::move(token)]()
      {
         auto spProcessor = processor.lock(); // TODO: think about lock.. shared_ptr (better performance)?
         if (!spProcessor)
         {
            return;
         }

         if (!token.IsCancellationRequested())
         {
            if (auto spValueSource = valueSource.lock())
            {
               if (spValueSource->HasValue()) // TODO: really need this check?
               {
                  const auto& [key, value] = spValueSource->GetValue();
                  spProcessor->GetConsumer()->Consume(key, value);
                  spValueSource->MoveNext();
               }
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

         while (!m_tasks.empty())
         {
            auto [task, token] = std::move(m_tasks.front());
            m_tasks.pop_front();

            if (token.IsCancellationRequested())
            {
               continue;
            }

            nextTask = std::move(task);
            break;
         }

         if (m_tasks.empty())
         {
            m_state = EState::free;
            return;
         }

         //task = std::move(m_tasks.front());
      }


      m_threadPool->Post(std::move(nextTask), m_id);
   }

   /// <summary>
   /// A new value available in the value source event handler
   /// </summary>
   void onNewValueAvailable(IValueSourcePtr<Key, Value> valueSource, CancellationToken token)
   {
      if (token.IsCancellationRequested())
      {
         return;
      }

      auto task = createTask(std::move(valueSource), token);

      {
         std::scoped_lock lock(m_mutex);
         if (m_state == EState::processing)
         {
            m_tasks.emplace_back(std::move(task), std::move(token));
            return;
         }

         assert(m_state == EState::free);
         m_state = EState::processing;
      }

      m_threadPool->Post(std::move(task), m_id);
   }

private:
   const IConsumerPtr<Key, Value> m_consumer;
   // a token is requiered in case a thread pool shall notify the consumer strictly from the same thread (STA simulation)
   const std::uintptr_t m_id; 
   std::mutex m_mutex; // guards m_state and m_tasks
   EState m_state = EState::free;
   mutable std::mutex m_valueSourceMutex; // guards m_valueSources
   using ValueSources = std::unordered_map<Key, std::tuple<IValueSourcePtr<Key, Value>, CancellationTokenSource>, Hash>;
   ValueSources m_valueSources;
   std::deque<std::tuple<std::packaged_task<void()>, CancellationToken>> m_tasks;
   const std::shared_ptr<TPool> m_threadPool;
};

template<typename Key, typename Value, typename TPool, typename Hash>
using ConsumerProcessorPtr = std::shared_ptr<ConsumerProcessor<Key, Value, TPool, Hash>>;
}
