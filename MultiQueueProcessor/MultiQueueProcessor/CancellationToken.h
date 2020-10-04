#pragma once

#include <memory>

namespace MQP
{

class CancellationTokenSource;

class CancellationToken
{
   friend CancellationTokenSource;
   explicit CancellationToken(std::weak_ptr<bool> isCancellationRequested)
      : m_isCancellationRequested(std::move(isCancellationRequested))
   {
   }

public:

   CancellationToken(const CancellationToken&) = default;
   CancellationToken& operator=(const CancellationToken&) = default;
   CancellationToken(CancellationToken&&) = default;
   CancellationToken& operator=(CancellationToken&&) = default;

   bool IsCancellationRequested() const noexcept
   {
      return m_isCancellationRequested.expired();
   }

private:
   std::weak_ptr<bool> m_isCancellationRequested;
};

class CancellationTokenSource
{
public:
   bool IsCancellationRequested() const
   {
      return *m_isCancellationRequested;
   }

   void Cancel()
   {
      *m_isCancellationRequested = true;
   }

   CancellationToken GetToken()
   {
      return CancellationToken(m_isCancellationRequested);
   }
   
private:
   std::shared_ptr<bool> m_isCancellationRequested = std::make_shared<bool>(false);
};

}
