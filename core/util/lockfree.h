// Fixed-size, lock-free, allocation-free queues and a triple buffer (SPEC §9.4 threading).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace vjn6 {

// Single-producer single-consumer ring. Capacity is a power of two; storage allocated once.
template <typename T>
class SpscQueue {
 public:
  explicit SpscQueue(std::size_t capacityPow2) : cap_(capacityPow2), mask_(capacityPow2 - 1), buf_(new T[capacityPow2]) {}
  bool push(const T& v) {
    const std::size_t h = head_.load(std::memory_order_relaxed);
    const std::size_t t = tail_.load(std::memory_order_acquire);
    if (h - t >= cap_) return false;
    buf_[h & mask_] = v;
    head_.store(h + 1, std::memory_order_release);
    return true;
  }
  bool pop(T& out) {
    const std::size_t t = tail_.load(std::memory_order_relaxed);
    const std::size_t h = head_.load(std::memory_order_acquire);
    if (t == h) return false;
    out = buf_[t & mask_];
    tail_.store(t + 1, std::memory_order_release);
    return true;
  }
  std::size_t size() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }
  std::size_t capacity() const { return cap_; }

 private:
  const std::size_t cap_, mask_;
  std::unique_ptr<T[]> buf_;
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

// Triple buffer: one writer publishes complete values, one reader takes the newest.
template <typename T>
class TripleBuffer {
 public:
  TripleBuffer() : bufs_(new T[3]) {}
  T& back() { return bufs_[back_]; }
  void publish() {
    // swap back with middle, mark fresh
    const int old = middle_.exchange(back_ | kFresh, std::memory_order_acq_rel);
    back_ = old & 3;
  }
  // Returns true if a newer value was taken.
  bool update() {
    if (!(middle_.load(std::memory_order_acquire) & kFresh)) return false;
    const int old = middle_.exchange(front_, std::memory_order_acq_rel);
    front_ = old & 3;
    return true;
  }
  const T& front() const { return bufs_[front_]; }

 private:
  static constexpr int kFresh = 4;
  std::unique_ptr<T[]> bufs_;
  int back_ = 0;
  std::atomic<int> middle_{1};
  int front_ = 2;
};

}  // namespace vjn6
