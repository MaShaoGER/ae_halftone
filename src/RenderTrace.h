#pragma once
// Optional development instrumentation. Completely absent from release builds.
#ifdef PATTERN_MFR_TRACE_PATH
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
namespace {
std::atomic<int> traceActive{0};
struct RenderTrace {
    long long begin;
    long frame, scale;
    int active;
    double gpuMs=-1;
    void recordGPU(double ms) { gpuMs=ms; }
    explicit RenderTrace(PF_InData* data)
        : begin(now()), frame(data->current_time), scale(data->time_scale),
          active(traceActive.fetch_add(1, std::memory_order_relaxed) + 1) {}
    static long long now() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    ~RenderTrace() {
        const long long end = now();
        traceActive.fetch_sub(1, std::memory_order_relaxed);
        uint64_t tid = 0;
        pthread_threadid_np(nullptr, &tid);
        char line[320];
        const int count = std::snprintf(line, sizeof(line),
            "{\"pid\":%d,\"thread\":%llu,\"frame\":%ld,\"scale\":%ld,\"start_ns\":%lld,\"end_ns\":%lld,\"active_at_start\":%d,\"metal_ms\":%.3f}\n",
            getpid(), static_cast<unsigned long long>(tid), frame, scale, begin, end, active,gpuMs);
        const int fd = open(PATTERN_MFR_TRACE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (fd >= 0) {
            if (count > 0 && count < static_cast<int>(sizeof(line))) (void)write(fd, line, count);
            close(fd);
        }
    }
};
}
#else
struct RenderTrace { explicit RenderTrace(PF_InData*) {} void recordGPU(double) {} };
#endif
