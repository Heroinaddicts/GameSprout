#pragma once

#include <cstddef>
#include <memory>

namespace GameSproutRuntime {

// The Runtime owner calls these methods and joins each batch before using its results.
// Tasks run on disjoint ranges and must not call Runtime, GRHI, or this worker group.
class RuntimeRenderWorkers final {
public:
    using BlockTask=void (*)(void* context,std::size_t begin,std::size_t end) noexcept;

    RuntimeRenderWorkers() noexcept;
    ~RuntimeRenderWorkers();
    RuntimeRenderWorkers(const RuntimeRenderWorkers&)=delete;
    RuntimeRenderWorkers& operator=(const RuntimeRenderWorkers&)=delete;

    unsigned WorkerCount() noexcept;
    void RunBlocks(std::size_t count,void* context,BlockTask task) noexcept;

private:
    struct State;
    std::unique_ptr<State> _State;
    bool _Initialized=false;
};

}
