> 介绍:
> 手写一个线程池
> 
> 参考:
> https://www.bilibili.com/video/BV1Fb421H7ep/?p=3&t=279



###### 改进
- 任务列表采用优先级队列, 可以设置优先级. 优先级高的任务先执行, id小的先执行
- 线程池可以暂停, 恢复
- 暂停时, 线程函数不再执行任务, 可以继续提交任务.
- 销毁线程池前, 禁止提交, 并完成队列中的任务

## 一些概念
###### io密集和cpu密集

- io密集指的是 频繁地使用io, 而IO会导致阻塞
  - 因此它适合多线程
- cpu密集指的是, 程序需要长期使用cpu
  - 在单核cpu条件下, 不适合多线程
  - 在多核心的下, 可以多线程

###### 为何要使用线程池?
不使用线程池的缺点: 
- 线程的创建销毁是很慢的, 耗费(时间/空间)资源
- 线程切换上下文时, 也占用时间
  
- 线程池可以提取创建线程, 在任务开始前就能创建, 在任务结束后也不必销毁线程
- 线程池可以重复利用线程
- 线程池可以控制线程的数量, 防止线程过多导致资源耗尽
- 线程池可以提高响应速度

###### 如何确定线程数量?
- CPU 密集型任务
  - 特点：主要是进行大量的计算，几乎不涉及 I/O 操作，例如复杂的数学运算、图形处理等。
  - 确定方式：这种任务类型下，线程数量不宜过多，一般设置为 CPU 核心数加 1 就可以充分利用 CPU 资源，避免过多的线程上下文切换带来的开销。
  - 因为过多的线程会竞争 CPU 时间片，导致频繁的上下文切换，反而降低性能。

- I/O 密集型任务
  - 特点：涉及大量的 I/O 操作，如文件读写、网络通信等，这些操作往往会导致线程阻塞，等待 I/O 完成。
  - 确定方式：线程数 = CPU 核心数 * （1 + 平均等待时间 / 平均计算时间）。
  - 例如，如果你的计算机有 8 个 CPU 核心，平均等待时间为 5 秒，平均计算时间为 1 秒，那么线程数 = 8 *（1 + 5/1）= 48。但实际应用中，可以根据具体情况进行调整，比如先设置一个较大的线程数，然后通过性能测试来逐步优化

###### 两种模式的线程池
- 固定数量线程: fixed 模式
- 可动态增长的线程池: cached 模式

## 一些STL参考链接

条件变量 https://en.cppreference.com/w/cpp/thread/condition_variable

unique_lock https://en.cppreference.com/w/cpp/thread/unique_lock

std::unique_lock or std::lock_guard: Which Is Better?     https://www.geeksforgeeks.org/stdunique_lock-or-stdlock_guard-which-is-better/

packaged_task
https://en.cppreference.com/w/cpp/thread/packaged_task

async
https://en.cppreference.com/w/cpp/thread/async
  