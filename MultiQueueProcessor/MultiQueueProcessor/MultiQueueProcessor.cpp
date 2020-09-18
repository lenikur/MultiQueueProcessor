// MultiQueueProcessor.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <sstream>
#include <string>
#include <assert.h>

#include "ThreadPoolBoost.h"
#include "MultiQueueProcessor.h"
#include "UserTypes.h"

using namespace std::chrono;

/// <summary>
/// Simple consumer
/// </summary>
template <typename TKey, typename Value>
struct TTestConsumer : MQP::IConsumer<TKey, Value>
{
public:
   TTestConsumer(std::uint32_t expectedCallsCount)
      : ExpectedCallsCount(expectedCallsCount)
   {
   }

   void Consume(const TKey& key, const Value& value) noexcept override
   {
      std::stringstream ss;
      ss << "TTestConsumer::Consume (" << this << ") key: " << key << ", value: " << value << std::endl;
      std::cout << ss.str();

      --ExpectedCallsCount;
   }

   std::atomic_uint32_t ExpectedCallsCount;
};

using MQProcessor = MQP::MultiQueueProcessor<MyKey, MyVal, MQP::ThreadPoolBoost, MyHash>;
using Consumer = TTestConsumer<MyKey, MyVal>;
using ConsumerPtr = std::shared_ptr < TTestConsumer<MyKey, MyVal>>;

/// <summary>
/// The function shows how to use MQProcessor 
/// </summary>
void sample()
{
   MQProcessor processor{ std::make_unique<MQP::ThreadPoolBoost>() };

   const MyKey key{ 1 };

   constexpr std::uint32_t valuesCount = 10;
   auto consumer = std::make_shared<Consumer>(valuesCount);
   processor.Subscribe(key, consumer);

   for (int i = 0; i < valuesCount; ++i)
   {
      processor.Enqueue(key, MyVal{ std::to_string(i) });
   }

   while (true)
   {
      if (consumer->ExpectedCallsCount == 0)
      {
         return;
      }

      std::this_thread::yield();
   }
}

/// <summary>
/// The function shows that a count of copy-related actions in MQProcessor doesn't depend on consumers count
/// </summary>
enum class EDemo { lvalue, rvalue };
void demoValueCopiesCount(EDemo mode)
{
   MQProcessor processor{ std::make_unique<MQP::ThreadPoolBoost>() };

   constexpr std::uint32_t valuesCount = 10;
   constexpr std::uint32_t consumersCount = 10;

   const MyKey key{ 1 };

   std::vector<ConsumerPtr> consumers;
   for (int i = 0; i < consumersCount; ++i)
   {
      processor.Subscribe(key, consumers.emplace_back(std::make_shared<Consumer>(valuesCount)));
   }

   boost::asio::thread_pool pool;

   for (int i = 0; i < valuesCount; ++i)
   {
      if (mode == EDemo::lvalue)
      {
         MyVal value{ std::to_string(i) };
         boost::asio::post(pool, [&processor, value = value]()
            {
               processor.Enqueue(1, value); // lvalue
            });
      }
      else if (mode == EDemo::rvalue)
      {
         boost::asio::post(pool, [&processor, i = i]()
            {
               processor.Enqueue(1, MyVal{ std::to_string(i) }); // rvalue
            });
      }
   }

   pool.join();

   bool done = false;

   while (!done)
   {
      done = true;
      for (const auto& consumer : consumers)
      {
         done &= (consumer->ExpectedCallsCount == 0);
      }

      std::this_thread::yield();
   }

   // The both cases doesn't depend on consumers count
   // The following checks are for movable Value
   if (mode == EDemo::lvalue)
   {
      assert(MyVal::_copyAndCreateCallsCount ==
         (2 * valuesCount // <- test data impact
            + valuesCount)); // <- MQProcessor's impact in case lvalue passing to
   }
   else if (mode == EDemo::rvalue)
   {
      assert(MyVal::_copyAndCreateCallsCount == valuesCount); // <- test data impact
      // zero copy impact of MQProcessor in case rvalue passing to
   }
}


int main()
{
   std::cout << "******************* Sample *******************" << std::endl;
   sample();

   MyVal::_copyAndCreateCallsCount = 0; // reset
   std::cout << "******************* Lvalue demo *******************" << std::endl;
   demoValueCopiesCount(EDemo::lvalue);

   MyVal::_copyAndCreateCallsCount = 0; // reset
   std::cout << "******************* Rvalue demo *******************" << std::endl;
   demoValueCopiesCount(EDemo::rvalue);

   return 0;
}
