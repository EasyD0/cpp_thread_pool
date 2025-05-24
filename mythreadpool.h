#ifndef MYTHREADPOOL_H
#define MYTHREADPOOL_H

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <semaphore>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

template <size_t Thread_MAX_NUM = 10, size_t Task_MAX_NUM = 100>
class Thread_Pool;

class Thread {
	template <size_t, size_t>
	friend class Thread_Pool;

public:
	using ThreadFunc = std::function<void(int)>;

private:
	inline static int generateId_ = 0;
	ThreadFunc func_;  // 待执行的任务函数
	int threadId_;     // 线程id
	std::atomic_bool stop = false;

public:
	Thread(ThreadFunc func) : func_(func), threadId_(generateId_++) {}

	~Thread() = default;

	void start() {
		std::thread t(func_, threadId_, std::ref(stop));
		t.detach();
	}

	int getId() const { return threadId_; }
};

template <size_t Thread_MAX_NUM, size_t Task_MAX_NUM>
class Thread_Pool {
public:
	enum Priority : int { LOW = 0, MEDIUM = 1, HIGH = 2 };  // 任务优先级
private:
	using atomic_num = std::atomic_size_t;
	using Task_Type = std::function<void()>;
	using Task_Type_full = std::tuple<Priority, size_t, Task_Type>;  // 优先级, 任务ID, 打包的任务函数

	struct queue_compare {
		bool operator()(const Task_Type_full& lhs, const Task_Type_full& rhs) {
			auto p1 = std::get<0>(lhs);
			auto p2 = std::get<0>(rhs);
			if (p1 != p2) return p1 < p2;

			auto thread_id1 = std::get<1>(lhs);
			auto thread_id2 = std::get<1>(rhs);
			return thread_id1 > thread_id2;
		}
	};

	// 构造和析构
public:
	Thread_Pool() = default;

	~Thread_Pool() { remove_pool(); }

	// 线程池相关
private:
	atomic_num Thread_Num = 0;
	atomic_num Thread_Num_Max = Thread_MAX_NUM;
	atomic_num idleThread_Num = 0;  // 已分配的线程中, 未在执行任务的线程数
	std::atomic_bool isRunning = false;

	std::mutex Thread_Mutex;                                       // 用于保护线程池一致性
	std::unordered_map<int, std::unique_ptr<Thread>> Threads_Map;  // 存放线程的容器

public:
	void Thread_Func(const int thread_id, const bool& stop);
	void add_Thread();                                                        // 增加一个线程
	void remove_Thread(const int);                                            // 停止并回收一个线程
	void start(size_t initThreadSize = std::thread::hardware_concurrency());  // 启动线程池

	void stop_pool();  // 停止所有线程并回收, 然后不再继续执行任务队列中的任务, 但不停止提交任务

	void remove_pool() {  // 停止提交任务, 执行任务队列中的任务, 停止所有线程并回收.
		if (!isRunning && stop_submit) {
			if (Task_Num == 0)
				return;
			else {
				start();
			}
		}

		stop_submit = true;

		while (Task_Num > 0) {
			Task_notEmpty.notify_all();
		}

		isRunning = false;
	}

	size_t thread_num() const { return Thread_Num; }

	bool is_thread_full() const { return Thread_Num == Thread_Num_Max; }

	// 任务队列相关
private:
	std::counting_semaphore<Task_MAX_NUM> Task_Semaphore;
	inline static size_t Task_ID = 0;
	std::priority_queue<Task_Type_full, std::vector<Task_Type_full>, queue_compare> Task_Q;  // 存储任务的容器
	std::mutex Task_Q_Mutex;
	atomic_num Task_Num = 0;
	// atomic_num Task_Num_Max = 100;
	std::condition_variable Task_notEmpty;
	std::atomic_bool stop_submit = false;

public:
	size_t task_num() const { return Task_Num; }

	bool is_task_full() const { return Task_Num == Thread_MAX_NUM; }

public:
	template <typename Func, typename... Args>
	std::future<std::result_of_t<Func(Args...)>> submit_task(Func&& func, Args&&... args, Priority p = LOW, bool useasync = false);
};

// 提交任务, 向任务队列中增加任务, 优先级默认为LOW, 默认当队列满时,默认不使用async函数
template <size_t Thread_MAX_NUM, size_t Task_MAX_NUM>
template <typename Func, typename... Args>
auto Thread_Pool<Thread_MAX_NUM, Task_MAX_NUM>::submit_task(Func&& func, Args&&... args, Priority p,
                                                            bool useasync) -> std::future<std::result_of_t<Func(Args...)>> {
	using ReturnType = std::result_of_t<Func(Args...)>;  // 任务实际返回类型(不含future层)

	bool fault = false;

	if (!stop_submit) {
		std::cerr << "不允许提交了，无法提交任务" << std::endl;
		fault = true;
	} else if (!Task_Semaphore.try_acquire_for(std::chrono::seconds(1))) {
		std::cerr << "任务队列已满，无法提交任务" << std::endl;
		fault = true;
	}

	if (fault) {
		if (useasync) {
			std::cerr << "使用异步执行任务" << std::endl;
			return std::async(std::launch::deferred, std::forward<Func>(func), std::forward<Args>(args)...);
		} else {
			std::cerr << "将返回无效的future" << std::endl;
			return std::async(std::launch::deferred, []() -> ReturnType {
				return ReturnType{};
			});
		}
	}

	auto task_ptr = std::make_shared(std::packaged_task<ReturnType()>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...)));

	if (!task_ptr) {
		std::cerr << "无法建立任务" << std::endl;
		Task_Semaphore.release();

		// 返回一个空的结果
		auto task_ptr_tmp = std::make_shared(std::packaged_task<ReturnType()>([]() {
			return ReturnType{};
		}));
		(*task_ptr_tmp)();
		return task_ptr_tmp->get_future();
	}

	std::unique_lock<std::mutex> lock(Task_Q_Mutex);  // 保护任务队列入队操作
	Task_Q.emplace(p, Task_ID++, [task_ptr]() {
		(*task_ptr)();
	});  // 将任务打包为 std::function<void()> 函数对象

	++Task_Num;
	Task_notEmpty.notify_one();
	return task_ptr->get_future();
}

/*
为什么线程函数需要用条件变量通知?
如果采用while循环检查的话, 那么这个线程函数会一直占用CPU时间.
而采用条件变量通知的办法可以在没有接到通知且没有任务的时候释放CPU时间片.

最简单办法就是循环检查, 且如果检查到任务队列为空则休眠一段时间, 但是导致没有及时性.
*/
template <size_t Thread_MAX_NUM, size_t Task_MAX_NUM>
void Thread_Pool<Thread_MAX_NUM, Task_MAX_NUM>::Thread_Func(const int thread_id, const bool& stop) {
	int timeout_num = 0;  // 等待超时次数, 如果超时次数过多, 可以考虑释放线程资源
	while (!stop && isRunning) {
		Task_Type_full task;
		{
			std::unique_lock ul(Task_Q_Mutex);
			bool timeout = !Task_notEmpty.wait_for(ul, std::chrono::seconds(1), [&]() -> bool {
				return Task_Num > 0;
			});

			if (timeout) {  // 等待任务到来超时
				++timeout_num;

				// 如果多次超时, 说明任务队列一直为空, 则考虑释放线程资源
				if (timeout_num == 10)
					break;
				else
					continue;
			}

			// 有任务到来
			timeout_num = 0;
			if (Task_Num > 0) {
				--idleThread_Num;
				task = Task_Q.top();
				Task_Q.pop();
				--Task_Num;
				Task_Semaphore.release();
			}
		}

		Task_Type task_real = std::get<2>(task);
		task_real();
		// 执行后该线程变为空闲状态
		++idleThread_Num;
	}

	// 被停止了
	--Thread_Num;
	--idleThread_Num;
	std::cerr << "线程 " << thread_id << " 将被删除" << std::endl;

	// 直接在这里删除线程, 而不在 remove_Thread 释放资源,  就不会有同步问题了
	{
		std::lock_guard lg{Thread_Mutex};
		Threads_Map.erase(thread_id);  // 删除智能指针导致绑定对象Thread被删除,
	}
}

template <size_t Thread_MAX_NUM, size_t Task_MAX_NUM>
void Thread_Pool<Thread_MAX_NUM, Task_MAX_NUM>::add_Thread() {
	if (Thread_Num == Thread_Num_Max) {
		std::cerr << "线程池已满，无法添加新线程" << std::endl;
		return;
	}
	++Thread_Num;
	auto Thread_ptr = std::make_unique<Thread>(std::bind(&Thread_Pool::Thread_Func, this, std::placeholders::_1, std::placeholders::_2));

	if (!Thread_ptr) {
		std::cerr << "无法创建新线程" << std::endl;
		--Thread_Num;
		return;
	}

	int id = Thread_ptr->getId();

	{
		std::lock_guard lg{Thread_Mutex};
		Threads_Map[id] = std::move(Thread_ptr);
		Threads_Map[id]->start();
	}
}

template <size_t Thread_MAX_NUM, size_t Task_MAX_NUM>
void Thread_Pool<Thread_MAX_NUM, Task_MAX_NUM>::start(size_t initThreadSize) {
	if (isRunning) {
		std::cerr << "线程池已经启动" << std::endl;
		return;
	}

	if (!(initThreadSize > 0 && initThreadSize <= Thread_Num_Max)) {
		std::cerr << "线程池初始化线程数不合法" << std::endl;
		return;
	}

	isRunning = true;
	Thread_Num = initThreadSize;
	for (int i = 0; i < initThreadSize; ++i) {
		add_Thread();
	}
}

template <size_t Thread_MAX_NUM, size_t Task_MAX_NUM>
void Thread_Pool<Thread_MAX_NUM, Task_MAX_NUM>::stop_pool() {
	if (!isRunning) {
		std::cerr << "线程池已经关闭, 无需重复关闭" << std::endl;
		return;
	}

	isRunning = false;

	std::cerr << "线程池关闭了" << std::endl;
}

template <size_t Thread_MAX_NUM, size_t Task_MAX_NUM>
void Thread_Pool<Thread_MAX_NUM, Task_MAX_NUM>::remove_Thread(const int thread_id) {
	if (Threads_Map.find(thread_id) == Threads_Map.end()) {
		std::cerr << "要暂停的线程 " << thread_id << " 不存在" << std::endl;
		return;
	}

	Threads_Map[thread_id]->stop = true;
	// 这使得Thread_Func跳出循环, 然后再释放对应的线程资源
}

#endif  // MYTHREADPOOL_H