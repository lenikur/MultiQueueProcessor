#pragma once

#include <memory>
#include <functional>

namespace MQP
{

/// <summary>
/// The interface describes a value source
/// </summary>
template <typename Value>
class IValueSource
{
public:
   virtual ~IValueSource() = 0 {}

   /// <summary>
   /// Gets a current value
   /// </summary>
   virtual Value& GetValue() const = 0;

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
   /// Deactivates a value source
   /// </summary>
   virtual void Stop() = 0;

   /// <summary>
   /// Sets a new available value event handler.
   /// The event is raisen as indication of switching between "no available value" state to "value is available".
   /// </summary>
   virtual void SetNewValueAvailableHandler(std::function<void()> handler) = 0;
};

template <typename Value>
using IValueSourcePtr = std::shared_ptr<IValueSource<Value>>;
template <typename Value>
using IValueSourceWeakPtr = std::weak_ptr<IValueSource<Value>>;

}