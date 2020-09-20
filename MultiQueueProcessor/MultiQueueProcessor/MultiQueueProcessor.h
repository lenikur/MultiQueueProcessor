#pragma once

#include <mutex>
#include <unordered_map>
#include <shared_mutex>
#include <memory>

#include "ConsumerProcessorGroup.h"

namespace MQP
{
/// <summary>
/// Multi queue processor.
/// Makes a single copy of enqueued value in case it is an lvalue regardless of number of consumers for movable Value.
/// Makes no copy of enqueued value in case it is a rvalue regardless of number of consumers for movable Value.
/// </summary>
template<typename Key, typename Value, typename TPool, typename Hash = std::hash<typename Key>>
class MultiQueueProcessor
{
public:
   /// <summary>
   /// Ctor
   /// </summary>
   /// <param name="threadPool">A thread pool that is used for "consumers calls" tasks execution.</param>
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
      std::scoped_lock lock(m_mutex);

      auto it = m_consumerProcessorGroups.find(key);
      if (it != std::end(m_consumerProcessorGroups))
      {
         it->second->Add(std::move(consumer));
         return;
      }

      m_consumerProcessorGroups.emplace(key,
         std::make_shared<ConsumerProcessorGroup<Key, Value, TPool>>(key, m_threadPool, std::move(consumer)));
   }

   /// <summary>
   /// Unsubscribes a consumer from value notifications by the key.
   /// There is no guarantee that the consumer won't receive notifications immediately after the method call,
   /// but the consumer's life time is prolonged at least till the end of notification.
   /// </summary>
   void Unsubscribe(const Key& key, IConsumerPtr<Key, Value> consumer)
   {
      std::scoped_lock lock(m_mutex);

      auto it = m_consumerProcessorGroups.find(key);
      if (it == std::end(m_consumerProcessorGroups))
      {
         return;
      }

      if (it->second->IsEmpty())
      {
         m_consumerProcessorGroups.erase(it);
      }
   }

   /// <summary>
   /// Enqueue a value for a key.
   /// </summary>
   template <typename TValue>
   void Enqueue(const Key& key, TValue&& value)
   {
      ConsumerProcessorGroupPtr<Key, Value, TPool> processorsGroup;

      {
         std::shared_lock sharedLock(m_mutex);
         auto it = m_consumerProcessorGroups.find(key);
         if (it == std::end(m_consumerProcessorGroups))
         {
            return;
         }

         processorsGroup = it->second;
      }

      processorsGroup->Process(std::forward<TValue>(value));
   }

private:
   std::shared_mutex m_mutex; // guards m_consumerProcessorGroups
   std::unordered_map<Key, ConsumerProcessorGroupPtr<Key, Value, TPool>, Hash> m_consumerProcessorGroups;
   const std::shared_ptr<TPool> m_threadPool; // a thread pool that is used for "consumers calls" tasks execution
};
}
