#pragma once

#include <list>
#include <tuple>
#include <shared_mutex>
#include <functional>

#include <assert.h>

#include "IValueSource.h"

namespace MQP
{

template <typename Key, typename Value>
class DataManager;

template <typename Key, typename Value>
using DataManagerPtr = std::shared_ptr<DataManager<Key, Value>>;

/// <summary>
/// The class manages all comming values and provides an ability to pull values individualy for each consumer
/// </summary>
template <typename Key, typename Value>
class DataManager : public std::enable_shared_from_this<DataManager<Key, Value>>
{
   template <typename Value>
   using ValuesStorage = std::list<std::tuple<Value, std::uint32_t>>;

   /// <summary>
   /// The class implements IValueSource interface and controls sequantial reading for one consumer regardless others.
   /// </summary>
   template <typename Key, typename Value>
   class Locator : public IValueSource<Key, Value>, public std::enable_shared_from_this<Locator<Key, Value>>
   {
      friend DataManager<Key, Value>;
   public:
      Locator(DataManagerPtr<Key, Value> dataManager, typename ValuesStorage<Value>::iterator position)
         : m_dataManager(dataManager)
         , m_position(position)
      {
      }

      ~Locator()
      {
         Stop();
      }

      Locator(const Locator&) = delete;
      Locator& operator=(const Locator&) = delete;
      Locator(Locator&&) = delete;
      Locator& operator=(Locator&&) = delete;

      std::tuple<const Key&, const Value&> GetValue() const override
      {
         return m_dataManager->getValue(m_position);
      }

      bool MoveNext() override
      {
         return m_dataManager->moveNext(m_position);
      }

      bool HasValue() const override
      {
         return m_dataManager->hasValue(m_position);
      }

      void Stop() override
      {
         m_dataManager->unregisterLocator(shared_from_this());
      }

      void SetNewValueAvailableHandler(std::function<void()> handler) override
      {
         m_newValueAvailableHandler = std::move(handler);
      }

      using std::enable_shared_from_this<Locator<Key, Value>>::shared_from_this;

   private:

      typename ValuesStorage<Value>::iterator& getPosition()
      {
         return m_position;
      }

      void onNewValueAvailable()
      {
         if (m_newValueAvailableHandler)
         {
            m_newValueAvailableHandler();
         }
      }

   private:
      DataManagerPtr<Key, Value> m_dataManager;
      typename ValuesStorage<Value>::iterator m_position;
      std::function<void()> m_newValueAvailableHandler;
   };

   template <typename Key, typename Value>
   using LocatorPtr = std::shared_ptr<Locator<Key, Value>>;

public:

   DataManager(Key key) : m_key(std::move(key))
   {}

   /// <summary>
   /// Adds new value
   /// </summary>
   template <typename TValue>
   void AddValue(TValue&& value)
   {
      std::vector<LocatorPtr<Key, Value>> locatorsForUpdate;

      {
         std::scoped_lock lock(m_mutex);

         m_values.emplace_back(std::forward<TValue>(value), 0);

         const auto& itBack = std::prev(std::end(m_values));

         for (auto& locator : m_locators)
         {
            auto& position = locator->getPosition();
            if (position == std::end(m_values))
            {
               position = itBack;
               ++(std::get<counter>(*position));

               locatorsForUpdate.push_back(locator);
            }
         }
      }

      for (const auto& locator : locatorsForUpdate)
      {
         locator->onNewValueAvailable();
      }
   }

   /// <summary>
   /// Creates new value source for a consumer
   /// </summary>
   IValueSourcePtr<Key, Value> CreateValueSource()
   {
      std::scoped_lock lock(m_mutex);

      // Regardless m_values emptiness a new locator alway points to the end, as all data in m_values is oldated for it
      return m_locators.emplace_back(std::make_shared<Locator<Key, Value>>(shared_from_this(), m_values.end()));
   }

   using std::enable_shared_from_this<DataManager<Key, Value>>::shared_from_this;

private:
   enum { value, counter };

   bool hasValue(typename const ValuesStorage<Value>::iterator& position) const
   {
      std::shared_lock lock(m_mutex);

      return position != std::end(m_values);
   }

   std::tuple<const Key&, const Value&> getValue(typename const ValuesStorage<Value>::iterator& position) const
   {
      std::shared_lock lock(m_mutex);
      assert(position != std::end(m_values));
      return { m_key, std::get<value>(*position) };
   }

   bool moveNext(typename ValuesStorage<Value>::iterator& position)
   {
      std::scoped_lock lock(m_mutex);

      assert(position != std::end(m_values));

      --(std::get<counter>(*position));
      const bool reachTheEnd = (++position == std::end(m_values));
      if (!reachTheEnd)
      {
         ++(std::get<counter>(*position));
      }

      collectUnusedValues();

      return !reachTheEnd;
   }

   void unregisterLocator(const LocatorPtr<Key, Value>& locator)
   {
      std::scoped_lock lock(m_mutex);

      auto it = std::find_if(std::begin(m_locators), std::end(m_locators), [&locator](const auto& loc)
         {
            return loc == locator;
         });

      if (it == std::end(m_locators))
      {
         return;
      }

      auto locatorPosition = (*it)->getPosition();
      m_locators.erase(it);

      if (locatorPosition == std::end(m_values))
      {
         return;
      }

      --(std::get<counter>(*locatorPosition));

      collectUnusedValues();
   }

   void collectUnusedValues()
   {
      for (auto it = std::begin(m_values); it != std::end(m_values);)
      {
         if (std::get<counter>(*it) != 0)
         {
            break;
         }

         auto removeIt = it++;
         m_values.erase(removeIt);
      }
   }

private:
   mutable std::shared_mutex m_mutex; // guards m_values and m_locators
   const Key m_key;
   ValuesStorage<Value> m_values;
   std::vector<LocatorPtr<Key, Value>> m_locators;
};

}