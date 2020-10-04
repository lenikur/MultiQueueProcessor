#pragma once

#include <memory>
#include <functional>

namespace MQP
{

template <typename Key, typename Value>
class IValueSource;

template <typename Key, typename Value>
using IValueSourcePtr = std::shared_ptr<IValueSource<Key, Value>>;
template <typename Key, typename Value>
using IValueSourceWeakPtr = std::weak_ptr<IValueSource<Key, Value>>;

/// <summary>
/// The interface describes a value source
/// </summary>
template <typename Key, typename Value>
class IValueSource
{
public:
   virtual ~IValueSource() = 0 {}

   /// <summary>
   /// Gets a current value
   /// </summary>
   virtual std::tuple<const Key&, const Value&> GetValue() const = 0; // TODO: unite GetValue and HasValue

   /// <summary>
   /// Checks whether a value is available in a source
   /// </summary>
   virtual bool HasValue() const = 0;

   /// <summary>
   /// Moves a source to the next value
   /// </summary>
   /// <returns>Whether a value is available after the completed movement</returns>
   virtual bool MoveNext() = 0;

   /// <summary>
   /// Deactivates a value source. Must be called by the interface consumer before desctruction.
   /// </summary>
   virtual void Stop() = 0;

   /// <summary>
   /// Whether the values source is stopped
   /// </summary>
   /// <returns></returns>
   virtual bool IsStopped() const = 0;

   /// <summary>
   /// A new available value handler type.
   /// </summary>
   using FnNewAvailableValueHandler = std::function<void(IValueSourcePtr<Key, Value> valueSource)>;
};

}