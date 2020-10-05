// The file shows some examples of MultiQueueProcessor using

#include "pch.h"

#include <iostream>
#include <sstream>
#include <string>
#include <atomic>
#include <assert.h>
#include <chrono>


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

namespace
{
   /// <summary>
   /// the value defines MQProcessor tuning parameter and is considered during samples launching (see main() implementation)
   /// </summary>
   constexpr MQP::ETuning multiQueueTuning = MQP::ETuning::size;
}

using MQProcessor = MQP::MultiQueueProcessor<MyKey, MyVal, MQP::ThreadPoolBoost, multiQueueTuning, MyHash>;
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
/// The function shows how to use MQProcessor. One consumer subscribed to 2 keys.
/// </summary>
void sampleOneSubscriberManyKeys()
{
   MQProcessor processor{ std::make_unique<MQP::ThreadPoolBoost>() };

   const MyKey key1{ 1 };
   const MyKey key2{ 2 };

   constexpr std::uint32_t valuesCount = 10;
   auto consumer = std::make_shared<Consumer>(valuesCount * 2);
   processor.Subscribe(key1, consumer);
   processor.Subscribe(key2, consumer);

   boost::asio::thread_pool pool;

   for (int i = 0; i < valuesCount; ++i)
   {
      MyVal value{ std::to_string(i) };
      boost::asio::post(pool, [&processor, key = key1, value = value]()
         {
            processor.Enqueue(key, value);
         });

      boost::asio::post(pool, [&processor, key = key2, value = value]()
         {
            std::this_thread::sleep_for(50ms);
            processor.Enqueue(key, value);
         });
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

   std::cout << "********** Sample one consumer many keys **********" << std::endl;
   sampleOneSubscriberManyKeys();

   if (multiQueueTuning == MQP::ETuning::size)
   {
      MyVal::_copyAndCreateCallsCount = 0; // reset
      std::cout << "******************* Lvalue demo *******************" << std::endl;
      demoValueCopiesCount(EDemo::lvalue);

      MyVal::_copyAndCreateCallsCount = 0; // reset
      std::cout << "******************* Rvalue demo *******************" << std::endl;
      demoValueCopiesCount(EDemo::rvalue);
   }

   return 0;
}
