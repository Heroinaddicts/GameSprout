#include <QApplication>
#include <QTimer>
#include <cstdlib>
#include "GameSproutFinal.h"
#if defined(Q_OS_MACOS)
#include "CocoaEditorFramePump.h"
#endif


int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

#if defined(Q_OS_MACOS)
    GameSproutEditor::CocoaEditorFramePump framePump;
    QObject::connect(&app, &QApplication::aboutToQuit, &framePump,
        &GameSproutEditor::CocoaEditorFramePump::Stop);
#endif
    GameSproutEditor::GameSproutFinal * gsf = GameSproutEditor::GameSproutFinal::GetInstance();
    if (!gsf) {
        return EXIT_FAILURE;
    }

#if defined(Q_OS_MACOS)
    framePump.Start(*gsf);
#else
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);

    QObject::connect(&timer, &QTimer::timeout, &app, [gsf]() {
        gsf->Update();
    });

    timer.start(16); // 每 16ms 调用一次，约 60 次/秒
#endif

    return QApplication::exec();
}
