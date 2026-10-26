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
#include "thread_pool.hpp"

std::mutex cout_m; // global lock for writing to terminal

int main(){
    { // scope for thread pool
        ThreadPool pool(4,8); // initialize a thread pool

        // submit tasks 
        for(int i=1; i<=20; ++i){
            pool.submit([i]{
                std::ostringstream os; // creates a string
                os << "Thread id : " << std::this_thread::get_id() << " and task id : " << i << "\n"; 
                {
                    std::lock_guard<std::mutex> lock(cout_m); // acquire the lock for cout
                    std::cout << os.str(); // prints the string
                } // release lock before sleeping
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            });
        }
    } // call destructor

    std::cout << "ALL DONE!!\n";

    return 0;
}