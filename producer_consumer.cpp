#include <iostream>
#include <mutex>
#include <thread>
#include <functional>
#include <chrono>
#include "queue.hpp"

std::mutex cout_m; // global lock for writing to terminal

// producer thread
void producer_work(Queue<int>& q){
    int item = 1; // items to be pushed starting from 1

    for(int i=0; i<10; ++i){
        if(q.push(item)){ // pushes the item. blocks while the queue is full
            std::lock_guard<std::mutex> lock(cout_m); // acquire the lock for cout
            std::cout << "[Producer] added : " << item++ << "\n"; // prints the items pushed and increment it by 1 so next time item is different
        }
    }
    q.close(); // no more items are coming, so wake the consumer when the queue is empty
}

// consumer thread
void consumer_work(Queue<int>& q){
    int out;
    while(q.pop(out)){ // blocks while the queue is empty. returns false when it is closed and empty
        {
            std::lock_guard<std::mutex> lock(cout_m); // acquire the lock for cout
            std::cout << "[Consumer] took : " << out << "\n"; // prints the items taken
        } // release lock before sleeping
        std::this_thread::sleep_for(std::chrono::milliseconds(300)); // consumer sleeps for 300ms before taking again
    }
}


int main(){
    // declare a integer queue
    Queue<int> q(2); // it gives RAII; lives on stack and cleanup itself

    std::thread producer(producer_work, std::ref(q));
    std::thread consumer(consumer_work, std::ref(q));

    producer.join();
    consumer.join();

    return 0;
}