#pragma once

#include <vector>
#include <algorithm>
#include <shared_mutex>
#include <memory>

#include "ConsumerProcessor.h"
#include "DataManager.h"

namespace MQP
{
/// <summary>
/// The class is responsible for providing an access to consumer processors that maintain consumers that
/// are subscribed to the same key.
/// </summary>
template<typename Key, typename Value, typename TPool>
class ConsumerProcessorGroup
{
public:
   ConsumerProcessorGroup(Key key, std::shared_ptr<TPool> threadPool)
      : m_key(std::move(key))
      , m_threadPool(std::move(threadPool))
      , m_dataManager(std::make_shared<DataManager<Value>>())
   {}

   ConsumerProcessorGroup(Key key, std::shared_ptr<TPool> threadPool, IConsumerPtr<Key, Value> consumer)
      : ConsumerProcessorGroup(key, threadPool)
   {
      Add(std::move(consumer));
   }

   ConsumerProcessorGroup(const ConsumerProcessorGroup&) = delete;
   ConsumerProcessorGroup& operator=(const ConsumerProcessorGroup&) = delete;
   ConsumerProcessorGroup(ConsumerProcessorGroup&&) = delete;
   ConsumerProcessorGroup& operator=(ConsumerProcessorGroup&&) = delete;

   template <typename TValue>
   void Process(TValue&& value)
   {
      m_dataManager->AddValue(std::forward<TValue>(value));
   }

   /// <summary>
   /// Adds a new consumer
   /// </summary>
   void Add(IConsumerPtr<Key, Value> consumer)
   {
      std::scoped_lock lock(m_mutex);

      // Ensure that the same consumer is subscribed only once per one key.
      // The check could be removed but it looks like the most expected behavior.
      if (std::end(m_processors) != std::find_if(std::begin(m_processors), std::end(m_processors),
         [&consumer](const auto& processor) { return processor->GetConsumer() == consumer; }))
      {
         return;
      }

      m_processors.emplace_back(std::make_shared<ConsumerProcessor<Key, Value, TPool>>(
         m_key, std::move(consumer), m_dataManager->CreateValueSource(), m_threadPool))->ConnectToValueSource();
   }

   /// <summary>
   /// Removes a consumer from a group and returns whether the group is empty after the call.
   /// </summary>
   void Remove(IConsumerPtr<Key, Value> consumer, bool& isEmpty)
   {
      std::scoped_lock lock(m_mutex);

      auto it = std::find_if(std::begin(m_processors), std::end(m_processors),
         [&consumer](const auto& processor) { return processor->GetConsumer() == consumer; });

      if (std::end(m_processors) != it)
      {
         m_processors.erase(it);
      }

      isEmpty = m_processors.empty();
   }

   /// <summary>
   /// Checks whether a group is empty.
   /// </summary>
   bool IsEmpty() const
   {
      std::shared_lock lock(m_mutex);
      return m_processors.empty();
   }

private:
   DataManagerPtr<Value> m_dataManager;
   mutable std::shared_mutex m_mutex; // Guards m_processors
   std::vector<ConsumerProcessorPtr<Key, Value, TPool>> m_processors;
   const Key m_key;
   const std::shared_ptr<TPool> m_threadPool;
};

template<typename Key, typename Value, typename TPool>
using ConsumerProcessorGroupPtr = std::shared_ptr<ConsumerProcessorGroup<Key, Value, TPool>>;
}
