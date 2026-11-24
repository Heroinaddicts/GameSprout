#pragma once

#include "ILightingAuthoring.h"
#include "ILightingBaker.h"
#include "IProjectSettings.h"
#include <QDialog>
#include <memory>

class IAssetsManager;
class ILibraryManager;
class ISelectionManager;
namespace Ui {class SpatialLightingForm;}

// Keep selection and edit drafts local. Scene state and Undo belong to LightingAuthoring.
class SpatialLightingWindow final : public QDialog,public ILightingEnvironmentListener,
    public IProjectSettingsListener,public ILightingBakeListener {
public:
    explicit SpatialLightingWindow(QWidget* parent=nullptr);
    ~SpatialLightingWindow() override;
    bool Bind(ILightingAuthoring*,IProjectSettings*,IAssetsManager*,ILibraryManager*,ISelectionManager*,
        ILightingBaker* baker=nullptr);
    void Shutdown();
    QString WindowMenuTitle() const;
    void OnEnvironmentLightingChanged(const EnvironmentLightingSnapshot&) override;
    void OnProjectSettingsChanged(const GameSproutRuntime::RuntimeProjectSettings&,std::uint64_t) override;
    void OnLightingBakeChanged(const LightingBakeState&) override;
private:
    enum class VolumeKind {Reflection,Indirect};
    void Refresh();
    void ChangeVolume(VolumeKind,bool add);
    void CommitPlanar();
    bool CommitProbe();
    bool CommitIndirect();
    bool Commit(const GameSproutRuntime::EnvironmentLightingSettings&);
    void BeginBake(VolumeKind);
    void RefreshBakeControls();
    void SetStatus(const QString&);
    bool CanEdit() const;
    std::unique_ptr<Ui::SpatialLightingForm> _Ui;
    ILightingAuthoring* _Authoring=nullptr;
    ILightingBaker* _Baker=nullptr;
    IProjectSettings* _ProjectSettings=nullptr;
    EnvironmentLightingSnapshot _Snapshot;
    LightingBakeState _BakeState;
    int _ProbeIndex=-1,_IndirectIndex=-1;
    std::uint64_t _ProbeContext=0,_IndirectContext=0;
    bool _Refreshing=false,_EnvironmentSubscribed=false,_ProjectSubscribed=false,_BakeSubscribed=false;
};
