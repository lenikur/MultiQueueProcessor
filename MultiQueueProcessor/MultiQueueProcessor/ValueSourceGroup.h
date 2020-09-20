#pragma once

#include <mutex>
#include <list>
#include <tuple>

#include "IValueSource.h"

namespace MQP
{

/// <summary>
/// The class groups sources for different keys and provides an ability to work with them as with a single value source
/// </summary>
template <typename Key, typename Value>
class ValueSourceGroup : public IValueSource<Key, Value>, public std::enable_shared_from_this<ValueSourceGroup<Key, Value>>
{
   enum {key, valueSource};
public:
   void AddValueSource(const Key& newKey, IValueSourcePtr<Key, Value> valueSource)
   {
      std::scoped_lock lock(m_mutex);

      for (const auto& [key, valueSource] : m_valueSources)
      {
         if (key == newKey)
         {
            return;
         }
      }

      valueSource->SetNewValueAvailableHandler([aggregator = weak_from_this()]()
      {
         if (auto spAggregator = aggregator.lock())
         {
            spAggregator->onNewValueAvailable();
         }
      });

      m_valueSources.emplace_back(newKey, std::move(valueSource));
   }

   void RemoveValueSource(const Key& key)
   {
      IValueSourcePtr removedValueSource;
      
      std::scoped_lock lock(m_mutex);
      {
         auto it = std::find_if(std::begin(m_valueSources), std::end(m_valueSources), [key](const auto& vs)
         {
            return key == std::get<key>(vs);
         });

         removedValueSource = std::get<valueSource>(*it);

         if (it == m_activeValueSource)
         {
            m_activeValueSource = std::end(m_valueSources);
         }

         m_valueSources.erase(it);
      }

      removedValueSource->Stop();
   }

   bool IsEmpty() const
   {
      std::scoped_lock lock(m_mutex); // TODO: shared_lock
      return m_valueSources.empty();
   }

   std::tuple<const Key&, const Value&> GetValue() const override
   {
      std::scoped_lock lock(m_mutex); // TODO: shared_lock
      {
         assert(hasValue()); // TODO: throw?

         do {
            if (m_activeValueSource == std::end(m_valueSources)) // only initial
            {
               m_activeValueSource = std::begin(m_valueSources);
            }
            else
            {
               ++m_activeValueSource;
               if (m_activeValueSource == std::end(m_valueSources))
               {
                  m_activeValueSource = std::begin(m_valueSources);
               }
            }
         } while (std::get<valueSource>(*m_activeValueSource)->HasValue());

         return std::get<valueSource>(*m_activeValueSource)->GetValue(); // TODO: call out of lock
      }
   }

   /// <summary>
   /// Checks whether a value is available in a source
   /// </summary>
   bool HasValue() const override
   {
      std::scoped_lock lock(m_mutex); // TODO: shared_lock

      return hasValue();
   }

   /// <summary>
   /// Moves a source to the next value
   /// </summary>
   /// <returns>Whether a value is available after the completed movement</returns>
   bool MoveNext() override
   {
      std::scoped_lock lock(m_mutex);
      std::get<valueSource>(*m_activeValueSource)->MoveNext();
      return hasValue();
   }

   /// <summary>
   /// Deactivates a value source
   /// </summary>
   void Stop() override
   {
      std::scoped_lock lock(m_mutex);

      for (const auto& [key, valueSource] : m_valueSources)
      {
         valueSource->Stop();
      }
   }

   /// <summary>
   /// Sets a new available value event handler.
   /// The event is raisen as indication of switching between "no available value" state to "value is available".
   /// </summary>
   void SetNewValueAvailableHandler(std::function<void()> handler) override
   {
      std::scoped_lock lock(m_mutex);

      m_newValueAvailableHandler = std::move(handler);
   }

private:
   using std::enable_shared_from_this<ValueSourceGroup<Key, Value>>::weak_from_this;

   /// <summary>
   /// A new value available in the value source event handler
   /// </summary>
   void onNewValueAvailable()
   {
      { // TODO: check MT safety
         std::function<void()> handler;

         std::scoped_lock lock(m_mutex);

         if (!m_newValueAvailableHandler)
         {
            return;
         }
      }

      m_newValueAvailableHandler();
   }

   bool hasValue() const
   {
      for (const auto& [key, valueSource] : m_valueSources)
      {
         if (valueSource->HasValue())
         {
            return true;
         }
      }

      return false;
   }

private:
   mutable std::mutex m_mutex;
   using ValueSources = std::list<std::tuple<Key, IValueSourcePtr<Key, Value>>>;
   ValueSources m_valueSources;
   mutable typename ValueSources::const_iterator m_activeValueSource;
   std::function<void()> m_newValueAvailableHandler;

};

template <typename Key, typename Value>
using ValueSourceGroupPtr = std::shared_ptr<ValueSourceGroup<Key, Value>>;

}