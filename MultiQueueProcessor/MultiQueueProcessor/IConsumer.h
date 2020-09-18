#pragma once

#include <memory>

namespace MQP
{

/// <summary>
/// The consumer's interace
/// </summary>
/// <typeparam name="Key"></typeparam>
/// <typeparam name="Value"></typeparam>
template<typename Key, typename Value>
struct IConsumer
{
   virtual void Consume(const Key& id, const Value& value) noexcept = 0;
};
   
template<typename Key, typename Value>
using IConsumerPtr = std::shared_ptr<IConsumer<Key, Value>>;

}
