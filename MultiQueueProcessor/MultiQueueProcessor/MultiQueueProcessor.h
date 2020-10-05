#pragma once

#include <mutex>
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <deque>
#include <type_traits>

#include "ConsumerProcessor.h"
#include "DataManager.h"
#include "DataManagerFavorSpeed.h"

namespace MQP
{

/// <summary>
/// MultiQueueProcessor's data management strategies
/// </summary>
enum class ETuning {size /*DataManager*/, speed /*DataManagerFavorSpeed*/};

/// <summary>
/// Multi queue processor
/// </summary>
template<typename Key, typename Value, typename TPool, ETuning TUNING, typename Hash = std::hash<typename Key>>
class MultiQueueProcessor
{
   enum {dataManager, subscribersToKey};

   /// <summary>
   /// "Data manager" class selection 
   /// </summary>
   using KeyDataManager = std::conditional_t<TUNING == ETuning::size, DataManager<Key, Value>, DataManagerFavorSpeed<Key, Value>>;
   using KeyDataManagerPtr = std::shared_ptr<KeyDataManager>;

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
   /// </summary>
   void Subscribe(const Key& key, IConsumerPtr<Key, Value> consumer)
   {
      if (!consumer)
      {
         return;
      }

      std::scoped_lock lock(m_mutex);

      auto itDataManager = m_dataManagers.find(key);
      if (itDataManager == std::end(m_dataManagers))
      {
         auto it = m_dataManagers.try_emplace(key, std::make_shared<KeyDataManager>(key), std::deque<IConsumerPtr<Key, Value>>{consumer});
         assert(it.second);
         itDataManager = it.first;
      }
      else
      {
         auto& subscribers = std::get<subscribersToKey>(itDataManager->second);
         if (std::find(std::begin(subscribers), std::end(subscribers), consumer) != std::end(subscribers))
         {
            // this consumer has already been subscribed to the passed key, prevent a double subscription
            return;
         }
      }

      auto [itConsumerProcessor, isInserted] = 
         m_consumerProcessors.emplace(consumer, std::make_shared<ConsumerProcessor<Key, Value, TPool, Hash>>(consumer, m_threadPool));

      // create and add a new value source to an existed consumer processor
      itConsumerProcessor->second->AddValueSource(key, std::get<dataManager>(itDataManager->second)->CreateValueSource(itConsumerProcessor->second));
   }

   /// <summary>
   /// Unsubscribes a consumer from value notifications by the key.
   /// There is no guarantee that the consumer won't receive notifications immediately after the method call,
   /// but the consumer's life time is prolonged at least till the end of the notification.
   /// </summary>
   void Unsubscribe(const Key& key, IConsumerPtr<Key, Value> consumer)
   {
      std::scoped_lock lock(m_mutex);

      const auto itConsumerProcessor = m_consumerProcessors.find(consumer);
      if (itConsumerProcessor == std::end(m_consumerProcessors))
      {
         // there is no such consumer
         return;
      }

      auto itDataManager = m_dataManagers.find(key);
      if (itDataManager == std::end(m_dataManagers))
      {
         // there is no such key
         return;
      }

      auto& subscribers = std::get<subscribersToKey>(itDataManager->second);

      auto itSubscriberToKey = std::find(std::begin(subscribers), std::end(subscribers), consumer);
      if (itSubscriberToKey == std::end(subscribers))
      {
         // this subscriber is not subscribed to the passed key
         assert(false);
         return;
      }

      subscribers.erase(itSubscriberToKey);

      if (subscribers.empty())
      {
         // there are no subscribers to the key, it's time to remove it
         m_dataManagers.erase(itDataManager);
      }

      auto consumerProcessor = itConsumerProcessor->second;
      consumerProcessor->RemoveSubscription(key);

      if (!consumerProcessor->IsSubscribedToAny())
      {
         m_consumerProcessors.erase(itConsumerProcessor);
      }
   }

   /// <summary>
   /// Enqueues a value for a key.
   /// </summary>
   template <typename TValue>
   void Enqueue(const Key& key, TValue&& value)
   {
      KeyDataManagerPtr keyDataManager;

      {
         std::shared_lock sharedLock(m_mutex);

         auto itDataManager = m_dataManagers.find(key);
         if (itDataManager == std::end(m_dataManagers))
         {
            return;
         }

         keyDataManager = std::get<dataManager>(itDataManager->second);
      }

      keyDataManager->AddValue(std::forward<TValue>(value));
   }

private:
   std::shared_mutex m_mutex; // guards m_consumerProcessors and m_dataManagers
   std::unordered_map<IConsumerPtr<Key, Value>, ConsumerProcessorPtr<Key, Value, TPool, Hash>> m_consumerProcessors;
   std::unordered_map<Key, std::tuple<KeyDataManagerPtr, std::deque<IConsumerPtr<Key, Value>>>, Hash> m_dataManagers;
   const std::shared_ptr<TPool> m_threadPool; // a thread pool that is used for "consumers calls" tasks execution
};
}
