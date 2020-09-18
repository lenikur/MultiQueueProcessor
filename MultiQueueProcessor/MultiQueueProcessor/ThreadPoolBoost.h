#pragma once

#include "boost/asio/thread_pool.hpp"
#include "boost/asio/post.hpp"

namespace MQP
{

/// <summary>
/// A thread pool wrapper for boost::asio::thread_pool.
/// The passed token is ignored.
/// </summary>
class ThreadPoolBoost
{
public:
   /// <summary>
   /// Posts a task to the thread pool
   /// </summary>
   /// <param name="task">A posted task</param>
   /// <param name="token">A token for tasks grouping</param>
   template <typename Task, typename Token>
   void Post(Task&& task, Token&& /*token*/)
   {
      boost::asio::post(m_threadPool, std::forward<Task>(task));
   }

   /// <summary>
   /// Stops the thread pool
   /// </summary>
   void Stop()
   {
      m_threadPool.stop();
      m_threadPool.join();
   }

private:
   boost::asio::thread_pool m_threadPool;
};

}
