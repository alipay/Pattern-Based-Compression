/*
 * Copyright 2023 The PBC Authors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef SRC_TRAIN_THREAD_POOL_H_
#define SRC_TRAIN_THREAD_POOL_H_

#include <condition_variable>  // NOLINT
#include <functional>          // NOLINT
#include <future>              // NOLINT
#include <memory>
#include <mutex>   // NOLINT
#include <queue>   // NOLINT
#include <thread>  // NOLINT
#include <utility>
#include <vector>

namespace PBC {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);

    template <class F, class... Args>
    auto SubmitTask(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            if (stop) {
                throw std::runtime_error("submit task on stopped ThreadPool");
            }

            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    ~ThreadPool();

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

}  // namespace PBC
#endif  // SRC_TRAIN_THREAD_POOL_H_
