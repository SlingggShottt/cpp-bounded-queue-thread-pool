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


// create a template for a class of our queue buffer where producers add items
template <typename T>
class Queue{

    private:
        // define a private queue using the template
        std::queue<T> q_;
        //capacity of queue
        const std::size_t capacity_;
        
        // to notify if the queue is closed or not
        bool is_closed_ = false;
        // a mutex for synchronization
        mutable std::mutex m_;

        // a condition variable to add notify and wait functionality
        std::condition_variable not_empty_;
        std::condition_variable not_full_;

    public :
        // an explicit constructor
        explicit Queue(std::size_t capacity) : capacity_(capacity){
            if(capacity == 0)
                throw std::invalid_argument("Capacity should be > 0!!");
        }

        // adding an item
        bool push(const T& item){
            { // lock scope starts
                std::unique_lock<std::mutex> lock(m_); // acquire the lock 
                // check is the queue is not full or closed
                not_full_.wait(lock, [&]{return (q_.size() < capacity_) || is_closed_;}); // take lock is the queue is not full and not closed
                // print full is the queue is full
                if(is_closed_) {
                    return false;
                }
                q_.push(item); // push item onto the back
            } // lock scope ends. released using RAII
            not_empty_.notify_one(); // notify a waiting consumer.
            return true;
        } 


        bool try_pop(T& out){
            { // lock scope starts
                std::lock_guard<std::mutex> lock(m_); // acquire the lock
                    // if the queue is empty return false
                    if(q_.empty()) return false;
                
                    // else copy front item to out
                    out = q_.front();
                    // pop the front item
                    q_.pop();
            } // lock released using RAII 
                    // notify producer the queue is not full
                    not_full_.notify_one(); 
                    // and return true
                    return true;
        }


        bool pop(T& out){
            {
            std::unique_lock<std::mutex> lock(m_); // acquire the lock
            
            not_empty_.wait(lock, [&]{return !q_.empty() || is_closed_;}); // take the lock if the queue is not empty and not closed
            // [&] means “capture surrounding names by reference.” Here it captures 'this', that's how the lambda can see q_.
            // wait until the queue is non-empty or closed. then take the unique lock

            // print empty is the queue is empty
            if(q_.empty()) {
                return false;
            }

            // copy the front item to out
            out = q_.front();
            // pop the front item
            q_.pop();
            } // lock released here using RAII
            not_full_.notify_one(); // notify a producer the queue is not full
            return true;
        }


        // std::size_t is an unsigned integer type defined in the C++ standard library (typically via <cstddef>) that is guaranteed to be large enough to represent the size of the largest possible object in bytes.
        std::size_t size() const { // const function means it won't change the object it is called upon
            std::lock_guard<std::mutex> lock(m_); // acquire a lock
                return q_.size();
        }// lock release using RAII

        // getter for queue capacity
        std::size_t get_capacity() const {
            return capacity_;
        }        

        void close(){
            { // lock scope starts
                std::lock_guard<std::mutex> lock(m_); // lock to change the state of is_closed_
                is_closed_ = true;
            } // lock released
            not_full_.notify_all();
            not_empty_.notify_all();
        }
};

