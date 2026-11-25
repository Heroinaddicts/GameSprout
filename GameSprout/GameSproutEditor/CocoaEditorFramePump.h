#pragma once

#include <QObject>
#include <QTimer>
#include <atomic>

namespace GameSproutEditor {
    class GameSproutFinal;

    class CocoaEditorFramePump final : public QObject {
    public:
        CocoaEditorFramePump();
        ~CocoaEditorFramePump() override;
        void Start(GameSproutFinal& editor);
        void Stop();

    private:
        void QueueDrawableWake();
        void Update(int nextFallbackMilliseconds);

        GameSproutFinal* _Editor = nullptr;
        QTimer _Fallback;
        std::atomic_bool _WakeQueued{false};
        bool _Running = true;
        bool _Updating = false;
    };
}
