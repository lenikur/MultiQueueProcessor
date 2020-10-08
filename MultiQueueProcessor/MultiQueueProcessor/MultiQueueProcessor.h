#pragma once

#include <unordered_map>
#include <shared_mutex>
#include <memory>

#include "ConsumerProcessor.h"
#include "IConsumer.h"

namespace MQP
{

/// <summary>
/// Multi queue processor
/// </summary>
template<typename Key, typename Value, typename TPool, typename Hash = std::hash<typename Key>>
class MultiQueueProcessor
{
public:
   /// <summary>
   /// Ctor
   /// </summary>
   /// <param name="threadPool">A thread pool that is used for the consumers notification tasks execution.</param>
   MultiQueueProcessor(std::shared_ptr<TPool> threadPool)
      : m_threadPool(std::move(threadPool))
   {}

   MultiQueueProcessor(const MultiQueueProcessor&) = delete;
   MultiQueueProcessor& operator=(const MultiQueueProcessor&) = delete;
   MultiQueueProcessor(MultiQueueProcessor&&) = delete;
   MultiQueueProcessor& operator=(MultiQueueProcessor&&) = delete;

   /// <summary>
   /// Subscribes a consumer to value notifications by the key.
   /// 
   /// It is guaranteed that a consumer will be notified (via IConsumer::Consume) sequentially (not simultaneously) 
   /// about all enqueued values for a key for which the consumer is subscribed to. Whether such notifications happen 
   /// in the same thread or the calls can occur from different threads (but anyway sequetially) is controlled by 
   /// the thread pool implementation, passed to MultiQueueProcessor.
   /// It is not guaranteed that the consumer which is subscribed to different keys will be notified sequentially
   /// about all enqueued values for that keys. The current implementation provides only "intra key" sequential notifications.
   /// </summary>
   void Subscribe(const Key& key, IConsumerPtr<Key, Value> consumer)
   {
      if (!consumer)
      {
         return;
      }

      std::scoped_lock lock(m_mutex);

      m_consumerProcessors.try_emplace(key, std::make_shared<ConsumerProcessor<Key, Value, TPool, Hash>>(key, std::move(consumer), m_threadPool));
   }

   /// <summary>
   /// Unsubscribes a consumer from value notifications by the key.
   /// There is no guarantee that the consumer won't receive notifications immediately after the method call,
   /// but the consumer's life time is prolonged at least till the end of the notification.
   /// </summary>
   void Unsubscribe(const Key& key, IConsumerPtr<Key, Value> consumer)
   {
      std::scoped_lock lock(m_mutex);

      m_consumerProcessors.erase(key);
   }

   /// <summary>
   /// Enqueues a value for a key.
   /// </summary>
   template <typename TValue>
   void Enqueue(const Key& key, TValue&& value)
   {
      ConsumerProcessorPtr<Key, Value, TPool, Hash> processor;
      {
         std::shared_lock sharedLock(m_mutex);

         auto itConsumerProcessor = m_consumerProcessors.find(key);
         if (itConsumerProcessor == std::end(m_consumerProcessors))
         {
            return;
         }

         processor = itConsumerProcessor->second;
      }

      processor->Enqueue(std::forward<TValue>(value));
   }

private:
   std::shared_mutex m_mutex; // guards m_consumerProcessors
   std::unordered_map<Key, ConsumerProcessorPtr<Key, Value, TPool, Hash>, Hash> m_consumerProcessors;
   const std::shared_ptr<TPool> m_threadPool; // a thread pool that is used for "consumers calls" tasks execution
};
}
