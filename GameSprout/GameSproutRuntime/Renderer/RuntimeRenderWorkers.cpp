#include "RuntimeRenderWorkers.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <utility>

namespace GameSproutRuntime {
namespace {

unsigned ParseWorkerCount(const char* value) noexcept {
    if(!value||(value[0]!='2'&&value[0]!='4')||value[1]!='\0')return 0;
    return static_cast<unsigned>(value[0]-'0');
}

unsigned ConfiguredWorkerCount() noexcept {
    constexpr char setting[]="GAMESPROUT_SKIN_BOUNDS_WORKERS";
#if defined(_MSC_VER)
    char* value=nullptr;
    std::size_t length=0;
    const auto status=_dupenv_s(&value,&length,setting);
    const bool configured=value&&*value;
    const auto count=status==0&&configured?ParseWorkerCount(value):0;
    std::free(value);
#else
    const auto* value=std::getenv(setting);
    const bool configured=value&&*value;
    const auto count=ParseWorkerCount(value);
#endif
    if(configured)return count;
#if defined(_WIN32)
    if(std::thread::hardware_concurrency()>=8)return 4;
#endif
    return 0;
}

}

struct RuntimeRenderWorkers::State {
    explicit State(unsigned count):Count(count) {}

    ~State() {
        {
            std::lock_guard lock(Mutex);
            Stopping=true;
        }
        WorkAvailable.notify_all();
        for(auto& thread:Threads)if(thread.joinable())thread.join();
    }

    void Start() {
        for(std::size_t index=0;index<Count;++index)
            Threads[index]=std::thread(&State::Work,this,index);
    }

    void Dispatch(std::size_t count,void* context,BlockTask task) noexcept {
        std::unique_lock lock(Mutex);
        Items=count;
        Active=Remaining=std::min(count-1,static_cast<std::size_t>(Count));
        Context=context;
        Task=task;
        ++Generation;
        WorkAvailable.notify_all();
        // The caller owns the final block and can work while the pool processes the others.
        const auto finalBlockSize=count/(Active+1);
        lock.unlock();
        task(context,count-finalBlockSize,count);
        lock.lock();
        WorkFinished.wait(lock,[this] {return Remaining==0;});
        Context=nullptr;
        Task=nullptr;
    }

    void Work(std::size_t index) noexcept {
        std::uint64_t seen=0;
        std::unique_lock lock(Mutex);
        for(;;) {
            WorkAvailable.wait(lock,[this,&seen] {return Stopping||seen!=Generation;});
            if(Stopping)return;
            seen=Generation;
            if(index>=Active)continue;
            const auto blockSize=Items/(Active+1);
            const auto extra=Items%(Active+1);
            const auto begin=index*blockSize+std::min(index,extra);
            const auto end=begin+blockSize+(index<extra?1u:0u);
            const auto task=Task;
            auto* context=Context;
            lock.unlock();
            task(context,begin,end);
            lock.lock();
            if(--Remaining==0)WorkFinished.notify_one();
        }
    }

    const unsigned Count;
    std::array<std::thread,4> Threads;
    std::mutex Mutex;
    std::condition_variable WorkAvailable,WorkFinished;
    std::uint64_t Generation=0;
    std::size_t Items=0,Active=0,Remaining=0;
    void* Context=nullptr;
    BlockTask Task=nullptr;
    bool Stopping=false;
};

RuntimeRenderWorkers::RuntimeRenderWorkers() noexcept=default;
RuntimeRenderWorkers::~RuntimeRenderWorkers()=default;

unsigned RuntimeRenderWorkers::WorkerCount() noexcept {
    if(!_Initialized) {
        _Initialized=true;
        const auto count=ConfiguredWorkerCount();
        if(count) {
            try {
                // Own the state before starting any thread so partial startup can join safely.
                auto state=std::make_unique<State>(count);
                state->Start();
                _State=std::move(state);
            } catch(...) {
                // Keep this Runtime on its serial path when a worker cannot start.
            }
        }
    }
    return _State?_State->Count:0;
}

void RuntimeRenderWorkers::RunBlocks(std::size_t count,void* context,BlockTask task) noexcept {
    if(!count)return;
    if(count==1||!WorkerCount()) {
        task(context,0,count);
        return;
    }
    _State->Dispatch(count,context,task);
}

}
