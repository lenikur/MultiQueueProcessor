#pragma once

#include <iostream>
#include <sstream>
#include <string>

/// <summary>
/// An example of class that could be used as Key type in MultiQueueProcessor
/// </summary>
struct MyKey
{
   int Value = 0;

   template <typename T>
   MyKey(T val) : Value(val)
   {
   }

   bool operator==(const MyKey& rhs) const noexcept
   {
      return Value == rhs.Value;
   }
};

std::ostream& operator<<(std::ostream& os, const MyKey& key)
{
   os << "<" << key.Value << ">";
   return os;
}

struct MyHash
{
   std::size_t operator()(const MyKey& key) const noexcept
   {
      return std::hash<int>{}(key.Value);
   }
};

/// <summary>
/// An example of class that could be used as Value type in MultiQueueProcessor
/// </summary>
struct MyVal
{
   MyVal()
   {
      ++_copyAndCreateCallsCount;
   }

   template <typename T>
   explicit MyVal(const T& s) : S(s)
   {
      ++_copyAndCreateCallsCount;
   }

   MyVal(const MyVal& rhs)
   {
      ++_copyAndCreateCallsCount;
      S = rhs.S;
   }

   MyVal& operator=(const MyVal& rhs)
   {
      ++_copyAndCreateCallsCount;
      S = rhs.S;
      return *this;
   }

   MyVal(MyVal&& rhs) noexcept
   {
      S = std::move(rhs.S);
   }

   MyVal & operator=(MyVal && rhs) noexcept
   {
      S = std::move(rhs.S);
      return *this;
   }

   bool operator==(const MyVal& rhs) const noexcept
   {
      return S == rhs.S;
   }

   std::string S;
   static std::atomic_uint32_t _copyAndCreateCallsCount;
};
std::atomic_uint32_t MyVal::_copyAndCreateCallsCount = 0;

std::ostream& operator<<(std::ostream& os, const MyVal& val)
{
   os << "[" << val.S << "]";
   return os;
}
