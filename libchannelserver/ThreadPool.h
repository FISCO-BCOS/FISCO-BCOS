#pragma once
#include <iostream>
#include <boost/thread/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <memory>

namespace dev {

class ThreadPool {
public:
	typedef std::shared_ptr<ThreadPool> Ptr;

	explicit ThreadPool(const std::string &threadName, size_t size) :
			_work(_ioService) {
		_threadName = threadName;

		for (size_t i = 0; i < size; ++i) {
			_workers.create_thread([&] {
				pthread_setThreadName(_threadName);
				_ioService.run();
			});
		}
	}

	~ThreadPool() {
		_ioService.stop();
		_workers.join_all();
	}

	// Add new work item to the pool.
	template<class F>
	void enqueue(F f) {
		_ioService.post(f);
	}

private:
	std::string _threadName;
	boost::thread_group _workers;
	boost::asio::io_service _ioService;
	boost::asio::io_service::work _work;
};

}
