#include "CocoaEditorFramePump.h"
#include "GameSproutFinal.h"

#include <QMetaObject>
#include <QScopedValueRollback>
#include <SDL3/SDL_gpu.h>

namespace {
    constexpr int EditorTimerMilliseconds = 16;
    // Wait just over one 60 Hz period for the next drawable notification.
    // If the window hides or the surface stops requesting frames, the timer
    // resumes normal Editor ticks even though no drawable will arrive.
    constexpr int DrawableWatchdogMilliseconds = 18;
}

namespace GameSproutEditor {
    CocoaEditorFramePump::CocoaEditorFramePump() {
        _Fallback.setSingleShot(true);
        _Fallback.setTimerType(Qt::PreciseTimer);
        connect(&_Fallback, &QTimer::timeout, this, [this] {
            Update(EditorTimerMilliseconds);
        });
        // Register before Editor initialization can claim any native surfaces.
        SDL_SetGPUMetalDrawableReadyCallback([](void* context) {
            static_cast<CocoaEditorFramePump*>(context)->QueueDrawableWake();
        }, this);
    }

    CocoaEditorFramePump::~CocoaEditorFramePump() {
        Stop();
    }

    void CocoaEditorFramePump::Start(GameSproutFinal& editor) {
        if (!_Running) return;
        _Editor = &editor;
        _Fallback.start(EditorTimerMilliseconds);
    }

    void CocoaEditorFramePump::Stop() {
        if (!_Running) return;
        _Running = false;
        _Fallback.stop();
        // SDL waits for any active notification to finish posting. Queued Qt
        // calls already posted will see _Running false or be removed on destruction.
        SDL_SetGPUMetalDrawableReadyCallback(nullptr, nullptr);
    }

    void CocoaEditorFramePump::QueueDrawableWake() {
        if (_WakeQueued.exchange(true, std::memory_order_relaxed)) return;
        // The drawable worker only queues a wake. All Editor work, simulation
        // and viewport rendering run in their usual order on the window thread.
        QMetaObject::invokeMethod(this, [this] {
            _WakeQueued.store(false, std::memory_order_relaxed);
            Update(DrawableWatchdogMilliseconds);
        }, Qt::QueuedConnection);
    }

    void CocoaEditorFramePump::Update(int nextFallbackMilliseconds) {
        if (!_Running || !_Editor) return;
        _Fallback.start(nextFallbackMilliseconds);
        if (_Updating) return;
        const QScopedValueRollback<bool> updating(_Updating, true);
        _Editor->Update();
    }
}
