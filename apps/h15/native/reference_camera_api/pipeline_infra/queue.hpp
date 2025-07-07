#pragma once

// General includes
#include <atomic>
#include <optional>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <string>
#include <memory>
#include <iostream>
#include "reference_camera_perfetto.hpp"

// Infra includes
#include "buffer.hpp"

class Queue
{
  private:
    std::queue<BufferPtr> m_queue;
    size_t m_max_buffers;
    bool m_leaky;
    bool m_print_level;
    std::string m_name;
    std::atomic<bool> m_flushing;
    std::unique_ptr<std::condition_variable> m_condvar;
    std::shared_ptr<std::mutex> m_mutex;
    uint64_t m_drop_count = 0, m_push_count = 0;
#ifdef HAVE_PERFETTO
    std::string m_counter_name;
    perfetto::CounterTrack m_counter_track;
#endif

  public:
    Queue(std::string name, size_t max_buffers, bool leaky = false, bool print_level = false);
    ~Queue();

    std::string name();
    int size();
    void push(BufferPtr buffer);
    BufferPtr pop();
    uint64_t check_timestamp(std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    void flush();
};

using QueuePtr = std::shared_ptr<Queue>;
