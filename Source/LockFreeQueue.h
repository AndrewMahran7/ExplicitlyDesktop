/*
  ==============================================================================

    LockFreeQueue.h
    Created: 9 Dec 2024
    Author: Explicitly Audio Systems

    Lock-free single-producer single-consumer queue for thread communication.
    
    Used for:
    - Audio thread → ASR thread: audio chunks
    - ASR thread → Audio thread: censorship timestamps

  ==============================================================================
*/

#pragma once

#include <atomic>
#include <array>
#include <optional>
#include <cstring>

/**
    Lock-free single-producer single-consumer (SPSC) queue.
    
    Thread Safety:
    - One writer thread (producer)
    - One reader thread (consumer)
    - No locks or mutexes
    - Uses atomic operations for synchronization
    
    Performance:
    - Push: O(1), wait-free
    - Pop: O(1), wait-free
    - No dynamic allocation after construction
    - Cache-friendly fixed-size array
    
    Template Parameters:
    - T: Data type to store
    - Capacity: Maximum number of elements (must be power of 2 for efficiency)
*/
template<typename T, size_t Capacity>
class LockFreeQueue
{
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    
    LockFreeQueue()
        : head_(0)
        , tail_(0)
    {
    }
    
    /**
        Push an element to the queue (producer thread).
        
        @param item     Element to push (by const reference)
        @return         true if successful, false if queue is full
        
        Thread: Producer only (e.g., audio thread)
    */
    bool push(const T& item)
    {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = increment(current_tail);
        
        if (next_tail == head_.load(std::memory_order_acquire))
        {
            // Queue is full
            return false;
        }
        
        // Simple assignment - T must be trivially copyable
        data_[current_tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        
        return true;
    }
    
    /**
        Push an element to the queue using move semantics (producer thread).
        
        @param item     Element to push (by rvalue reference)
        @return         true if successful, false if queue is full
        
        Thread: Producer only (e.g., audio thread)
    */
    bool push(T&& item)
    {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = increment(current_tail);
        
        if (next_tail == head_.load(std::memory_order_acquire))
        {
            // Queue is full
            return false;
        }
        
        data_[current_tail] = std::move(item);
        tail_.store(next_tail, std::memory_order_release);
        
        return true;
    }
    
    /**
        Pop an element from the queue (consumer thread).
        
        @return     Element if available, std::nullopt if queue is empty
        
        Thread: Consumer only (e.g., ASR thread)
    */
    std::optional<T> pop()
    {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        
        if (current_head == tail_.load(std::memory_order_acquire))
        {
            // Queue is empty
            return std::nullopt;
        }
        
        T item = data_[current_head];
        head_.store(increment(current_head), std::memory_order_release);
        
        return item;
    }
    
    /**
        Check if queue is empty.
        
        @return     true if empty
        
        Thread: Any thread
    */
    bool isEmpty() const
    {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }
    
    /**
        Check if queue is full.
        
        @return     true if full
        
        Thread: Any thread
    */
    bool isFull() const
    {
        const size_t next_tail = increment(tail_.load(std::memory_order_acquire));
        return next_tail == head_.load(std::memory_order_acquire);
    }
    
    /**
        Get approximate number of elements in queue.
        
        @return     Number of elements (approximate, may be stale)
        
        Thread: Any thread (for debugging/monitoring only)
    */
    size_t size() const
    {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (t >= h) ? (t - h) : (Capacity - h + t);
    }
    
private:
    /**
        Increment index with wraparound using modulo.
        Since Capacity is power of 2, we can use bitwise AND for fast modulo.
    */
    size_t increment(size_t idx) const
    {
        return (idx + 1) & (Capacity - 1);
    }
    
    // Padding to avoid false sharing between head and tail on different cache lines
    alignas(64) std::atomic<size_t> head_;  // Consumer index
    alignas(64) std::atomic<size_t> tail_;  // Producer index
    
    std::array<T, Capacity> data_;          // Fixed-size storage
};
