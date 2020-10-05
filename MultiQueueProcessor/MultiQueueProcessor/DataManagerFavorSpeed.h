#pragma once

#include <deque>
#include <tuple>
#include <mutex>

#include <assert.h>

#include "IValueSource.h"

namespace MQP
{

template <typename Key, typename Value>
class DataManagerFavorSpeed;

template <typename Key, typename Value>
using DataManagerFavorSpeedPtr = std::shared_ptr<DataManagerFavorSpeed<Key, Value>>;

/// <summary>
/// The class manages all incoming values and creates instances of IValueSource implementation (see DataManagerFavorSpeed::Locator).
/// Each Locator copies and keeps incoming value
/// </summary>
template <typename Key, typename Value>
class DataManagerFavorSpeed : public std::enable_shared_from_this<DataManagerFavorSpeed<Key, Value>>
{
   /// <summary>
   /// The class implements IValueSource interface and controls sequantial reading for one consumer regardless others.
   /// </summary>
   template <typename Key, typename Value>
   class Locator : public IValueSource<Key, Value>, public std::enable_shared_from_this<Locator<Key, Value>>
   {
      friend DataManagerFavorSpeed<Key, Value>;
   public:
      Locator(DataManagerFavorSpeedPtr<Key, Value> dataManager, IValueSourceConsumerPtr<Key, Value> consumer, const Key& key)
         : m_dataManager(std::move(dataManager))
         , m_consumer(std::move(consumer))
         , m_key(key)
      {
      }

      Locator(const Locator&) = delete;
      Locator& operator=(const Locator&) = delete;
      Locator(Locator&&) = delete;
      Locator& operator=(Locator&&) = delete;

      std::tuple<const Key&, const Value&> GetValue() const override
      {
         std::scoped_lock lock(m_mutex);

         assert(!m_values.empty());
         return { m_key, m_values.front() };
      }

      bool MoveNext() override
      {
         std::scoped_lock lock(m_mutex);

         m_values.pop_front();
         return !m_values.empty();
      }

      bool HasValue() const override
      {
         std::scoped_lock lock(m_mutex);

         return !m_values.empty();
      }

      void Stop() override
      {
         m_isStopRequested = true;
         m_dataManager->unsubscribeLocator(shared_from_this());
      }

      bool IsStopped() const override
      {
         return m_isStopRequested;
      }

   private:

      using std::enable_shared_from_this<Locator<Key, Value>>::shared_from_this;
      using std::enable_shared_from_this<Locator<Key, Value>>::weak_from_this;

      void onNewValueAvailable(const Value& value)
      {
         {
            std::scoped_lock lock(m_mutex);
            m_values.emplace_back(value);
         }

         if (auto spConsumer = m_consumer.lock())
         {
            spConsumer->OnNewValueAvailable(shared_from_this());
         }
      }

   private:
      std::atomic_bool m_isStopRequested = false;
      DataManagerFavorSpeedPtr<Key, Value> m_dataManager;
      const IValueSourceConsumerWeakPtr<Key, Value> m_consumer;
      mutable std::mutex m_mutex; // guards m_values
      std::deque<Value> m_values;
      const Key m_key;
   };

   template <typename Key, typename Value>
   using LocatorPtr = std::shared_ptr<Locator<Key, Value>>;

   template <typename Key, typename Value>
   using LocatorWeakPtr = std::weak_ptr<Locator<Key, Value>>;

public:

   DataManagerFavorSpeed(Key key) : m_key(std::move(key))
   {}

   /// <summary>
   /// Adds a new value
   /// </summary>
   template <typename TValue>
   void AddValue(TValue&& value)
   {
      std::vector<LocatorPtr<Key, Value>> locatorsForUpdate;

      {
         std::scoped_lock lock(m_mutex);

         locatorsForUpdate = m_locators;
      }

      for (const auto& locator : locatorsForUpdate)
      {
         locator->onNewValueAvailable(value);
      }
   }

   /// <summary>
   /// Creates a new value source for a consumer
   /// </summary>
   IValueSourcePtr<Key, Value> CreateValueSource(IValueSourceConsumerPtr<Key, Value> consumer)
   {
      std::scoped_lock lock(m_mutex);

      return m_locators.emplace_back(std::make_shared<Locator<Key, Value>>(shared_from_this(), std::move(consumer), m_key));
   }

   using std::enable_shared_from_this<DataManagerFavorSpeed<Key, Value>>::shared_from_this;

private:

   /// <summary>
   /// Unsubscribe the passed locator from updates
   /// The method still keeps available Locator::GetValue method correct work
   /// </summary>
   void unsubscribeLocator(LocatorPtr<Key, Value> locator)
   {
      LocatorPtr<Key, Value> unsubscribedLocator;

      {
         std::scoped_lock lock(m_mutex);

         auto itUnsubscribedLocator = std::find_if(std::begin(m_locators), std::end(m_locators), [&locator](const auto& loc)
            {
               return loc == locator;
            });

         if (itUnsubscribedLocator == std::end(m_locators))
         {
            assert(false); 
            return;
         }

         unsubscribedLocator = std::move(*itUnsubscribedLocator); // destroying out of the lock
         m_locators.erase(itUnsubscribedLocator);
      }
   }

private:
   mutable std::mutex m_mutex; // guards m_values and m_locators
   const Key m_key;
   std::vector<LocatorPtr<Key, Value>> m_locators;
};

}