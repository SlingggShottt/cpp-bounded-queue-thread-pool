#include <iostream>
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <functional>
#include <chrono>
#include <condition_variable>
#include <stdexcept>
#include <vector>
#include <sstream>
#pragma once
#include "queue.hpp"


// creating our thread pool
class ThreadPool{
    private:
        Queue<std::function<void()>> tasks_; // a queue or tasks
        std::vector<std::thread> workers_; // a vector of our worker threads

    public :
        ThreadPool(std::size_t n_threads, std::size_t queue_capacity) : tasks_(queue_capacity) {
            // start the threads
            for(std::size_t w=0; w<n_threads; ++w){
                workers_.emplace_back([this]{
                    std::function<void()> task; // a placeholder for the popped task
                    while(tasks_.pop(task)){ // while the task is popped
                        task(); // run the task
                    }
                });
            }
        }

        bool submit(std::function<void()> task){
            return tasks_.push(task);
        }

        // destructor who closes the queue and joins the threads
        ~ThreadPool(){
            // close the queue
            tasks_.close();

            // join all the threads
            for(auto& worker : workers_){
                worker.join();
            }
        }

        // a pool owns it mutex holding queue and threads so it shouldn't be copyable
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
};