#include "SpatialLightingWindow.h"
#include "InspectorNumberText.h"
#include "ui_SpatialLightingUI.h"
#include <QScopedValueRollback>
#include <QStandardItemModel>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace GameSproutRuntime;
namespace {
using VectorEditors=std::array<QLineEdit*,3>;

bool ReadFloat(QLineEdit* edit,float& value) {
    bool valid=false;
    const auto input=edit->text().trimmed().toDouble(&valid);
    if(!valid||!std::isfinite(input)||std::abs(input)>std::numeric_limits<float>::max())return false;
    value=static_cast<float>(input);
    return true;
}

bool ReadVector(const VectorEditors& editors,Vector3& value) {
    return ReadFloat(editors[0],value.X)&&ReadFloat(editors[1],value.Y)&&ReadFloat(editors[2],value.Z);
}

void WriteVector(const VectorEditors& editors,Vector3 value) {
    editors[0]->setText(InspectorNumbers::Format(value.X));
    editors[1]->setText(InspectorNumbers::Format(value.Y));
    editors[2]->setText(InspectorNumbers::Format(value.Z));
}

void UpdateList(QComboBox* combo,int& selected,int count,const QString& name) {
    if(combo->count()!=count) {
        combo->clear();
        for(int index=0;index<count;++index)combo->addItem(name.arg(index+1));
    }
    selected=count?std::clamp(selected,0,count-1):-1;
    combo->setCurrentIndex(selected);
}
}

SpatialLightingWindow::SpatialLightingWindow(QWidget* parent)
    :QDialog(parent),_Ui(std::make_unique<Ui::SpatialLightingForm>()) {
    _Ui->setupUi(this);
    connect(_Ui->closeButton,&QPushButton::clicked,this,&QDialog::close);
    const auto connectFloats=[this](auto commit,std::initializer_list<QLineEdit*> fields) {
        for(auto* field:fields)connect(field,&QLineEdit::editingFinished,this,commit);
    };
    const auto planar=[this]{CommitPlanar();};
    const auto probe=[this]{CommitProbe();};
    const auto indirect=[this]{CommitIndirect();};
    connectFloats(planar,{_Ui->planarHeight,_Ui->planarScale,_Ui->planarStrength,_Ui->planarDistance});
    connectFloats(probe,{_Ui->captureX,_Ui->captureY,_Ui->captureZ,_Ui->probeCenterX,_Ui->probeCenterY,
        _Ui->probeCenterZ,_Ui->probeSizeX,_Ui->probeSizeY,_Ui->probeSizeZ,_Ui->probeBlend,_Ui->probeIntensity,_Ui->probePriority});
    connectFloats(indirect,{_Ui->indirectCenterX,_Ui->indirectCenterY,_Ui->indirectCenterZ,
        _Ui->indirectSizeX,_Ui->indirectSizeY,_Ui->indirectSizeZ,_Ui->indirectBlend,_Ui->indirectIntensity});
    connect(_Ui->planarEnabled,&QCheckBox::toggled,this,planar);
    connect(_Ui->planarResolution,&QSpinBox::valueChanged,this,planar);
    connect(_Ui->probeEnabled,&QCheckBox::toggled,this,probe);
    connect(_Ui->probeProjection,&QCheckBox::toggled,this,probe);
    connect(_Ui->indirectPriority,&QSpinBox::valueChanged,this,indirect);
    _Ui->planarMask->MaskChanged=[this](std::uint32_t){CommitPlanar();};
    _Ui->probeMask->MaskChanged=[this](std::uint32_t){CommitProbe();};
    _Ui->probeAsset->Changed=[this](const ResourceReference&){CommitProbe();};
    _Ui->indirectAsset->Changed=[this](const ResourceReference&){CommitIndirect();};

    connect(_Ui->probeList,&QComboBox::currentIndexChanged,this,[this](int selected) {
        if(_Refreshing)return;
        _ProbeIndex=selected;SetStatus({});Refresh();
    });
    connect(_Ui->indirectList,&QComboBox::currentIndexChanged,this,[this](int selected) {
        if(_Refreshing)return;
        _IndirectIndex=selected;SetStatus({});Refresh();
    });
    for(const bool add:{true,false}) {
        connect(add?_Ui->probeAdd:_Ui->probeRemove,&QPushButton::clicked,this,[this,add]{ChangeVolume(VolumeKind::Reflection,add);});
        connect(add?_Ui->indirectAdd:_Ui->indirectRemove,&QPushButton::clicked,this,[this,add]{ChangeVolume(VolumeKind::Indirect,add);});
    }
    connect(_Ui->probeBake,&QPushButton::clicked,this,[this]{BeginBake(VolumeKind::Reflection);});
    connect(_Ui->indirectBake,&QPushButton::clicked,this,[this]{BeginBake(VolumeKind::Indirect);});
    connect(_Ui->cancelBake,&QPushButton::clicked,this,[this]{if(_Baker)_Baker->CancelBake();});
    const auto limitReflectionSize=[this] {
        const auto maxSize=_Ui->probeBakeResolution->currentText().toUInt();
        auto* sizes=_Ui->probeReflectionResolution;
        if(sizes->currentText().toUInt()>maxSize)sizes->setCurrentText(QString::number(maxSize));
        auto* model=qobject_cast<QStandardItemModel*>(sizes->model());
        if(model)for(int index=0;index<sizes->count();++index)
            model->item(index)->setEnabled(sizes->itemText(index).toUInt()<=maxSize);
    };
    connect(_Ui->probeBakeResolution,&QComboBox::currentIndexChanged,this,limitReflectionSize);
    limitReflectionSize();
    OnProjectSettingsChanged(RuntimeProjectSettings{},0);
    Refresh();SetStatus({});
}
SpatialLightingWindow::~SpatialLightingWindow(){Shutdown();}

bool SpatialLightingWindow::Bind(ILightingAuthoring* authoring,IProjectSettings* settings,IAssetsManager* assets,
    ILibraryManager* library,ISelectionManager* selection,ILightingBaker* baker) {
    Shutdown();
    if(!authoring)return false;
    _Authoring=authoring;
    _EnvironmentSubscribed=_Authoring->RegisterEnvironmentListener(this);
    if(!_EnvironmentSubscribed){Shutdown();return false;}
    _ProjectSettings=settings;
    if(_ProjectSettings) {
        _ProjectSubscribed=_ProjectSettings->RegisterSettingsListener(this);
        if(!_ProjectSubscribed){Shutdown();return false;}
        OnProjectSettingsChanged(_ProjectSettings->GetSettings(),_ProjectSettings->GetRevision());
    }
    _Baker=baker;
    if(_Baker) {
        _BakeSubscribed=_Baker->RegisterBakeListener(this);
        if(!_BakeSubscribed){Shutdown();return false;}
        OnLightingBakeChanged(_Baker->GetBakeState());
    }
    _Ui->probeAsset->Bind(assets,library,selection,eAssetType::Environment);
    _Ui->indirectAsset->Bind(assets,library,selection,eAssetType::Irradiance);
    EnvironmentLightingSnapshot snapshot;
    std::string error;
    if(!_Authoring->ReadEnvironment(snapshot,error)){Shutdown();return false;}
    OnEnvironmentLightingChanged(snapshot);
    return true;
}

void SpatialLightingWindow::Shutdown() {
    if(_BakeSubscribed)_Baker->UnregisterBakeListener(this);
    if(_EnvironmentSubscribed)_Authoring->UnregisterEnvironmentListener(this);
    if(_ProjectSubscribed)_ProjectSettings->UnregisterSettingsListener(this);
    _EnvironmentSubscribed=false;_ProjectSubscribed=false;_BakeSubscribed=false;
    _Authoring=nullptr;_ProjectSettings=nullptr;_Baker=nullptr;_BakeState={};
    _Ui->probeAsset->Bind(nullptr,nullptr,nullptr,eAssetType::Environment);
    _Ui->indirectAsset->Bind(nullptr,nullptr,nullptr,eAssetType::Irradiance);
    _Snapshot={};_ProbeIndex=_IndirectIndex=-1;
}

QString SpatialLightingWindow::WindowMenuTitle() const {return _Ui->windowMenuAction->text();}
bool SpatialLightingWindow::CanEdit() const {
    return !_Refreshing&&_Authoring&&_Snapshot.Available&&_Snapshot.Editable;
}

void SpatialLightingWindow::OnProjectSettingsChanged(const RuntimeProjectSettings& settings,std::uint64_t) {
    _Ui->planarMask->SetLayers(settings.LayerNames);
    _Ui->probeMask->SetLayers(settings.LayerNames);
}
void SpatialLightingWindow::OnEnvironmentLightingChanged(const EnvironmentLightingSnapshot& snapshot) {
    if(_Snapshot.SceneID!=snapshot.SceneID) {
        _ProbeIndex=_IndirectIndex=-1;
        SetStatus({});
    }
    if(_Snapshot.Settings.ReflectionProbes!=snapshot.Settings.ReflectionProbes)++_ProbeContext;
    if(_Snapshot.Settings.IndirectLightVolumes!=snapshot.Settings.IndirectLightVolumes)++_IndirectContext;
    _Snapshot=snapshot;
    Refresh();
}

void SpatialLightingWindow::Refresh() {
    const QScopedValueRollback updating(_Refreshing,true);
    const bool editable=_Snapshot.Available&&_Snapshot.Editable;
    _Ui->availabilityLabel->setVisible(!editable);
    _Ui->featureTabs->setEnabled(editable);
    const auto& settings=_Snapshot.Settings;
    const auto& planar=settings.PlanarReflection;
    _Ui->planarEnabled->setChecked(planar.Enabled);
    _Ui->planarHeight->setText(InspectorNumbers::Format(planar.Height));
    _Ui->planarScale->setText(InspectorNumbers::Format(planar.ResolutionScale));
    _Ui->planarStrength->setText(InspectorNumbers::Format(planar.Strength));
    _Ui->planarDistance->setText(InspectorNumbers::Format(planar.MaxDistance));
    _Ui->planarResolution->setValue(static_cast<int>(planar.MaxResolution));
    _Ui->planarMask->SetMask(planar.LayerMask);

    UpdateList(_Ui->probeList,_ProbeIndex,static_cast<int>(settings.ReflectionProbes.size()),tr("探针 %1"));
    UpdateList(_Ui->indirectList,_IndirectIndex,static_cast<int>(settings.IndirectLightVolumes.size()),tr("体积 %1"));
    _Ui->probeBody->setEnabled(_ProbeIndex>=0);
    _Ui->probeRemove->setEnabled(_ProbeIndex>=0);
    _Ui->indirectBody->setEnabled(_IndirectIndex>=0);
    _Ui->indirectRemove->setEnabled(_IndirectIndex>=0);
    _Ui->indirectAdd->setEnabled(settings.IndirectLightVolumes.size()<MaxBakedIndirectVolumes);
    RefreshBakeControls();

    const auto sceneToken=std::to_string(_Snapshot.SceneID)+(editable?":edit:":":inspect:");
    const auto syncAsset=[&](ResourceField* field,int index,std::uint64_t context,const ResourceReference& asset) {
        field->SetContextKey(sceneToken+std::to_string(index)+":"+std::to_string(context));
        field->SetValue(asset);
    };
    const auto probe=_ProbeIndex>=0?settings.ReflectionProbes[_ProbeIndex]:ReflectionProbeSettings{};
    syncAsset(_Ui->probeAsset,_ProbeIndex,_ProbeContext,probe.Environment);
    _Ui->probeEnabled->setChecked(probe.Enabled);
    _Ui->probeProjection->setChecked(probe.BoxProjection);
    WriteVector({_Ui->captureX,_Ui->captureY,_Ui->captureZ},probe.CapturePosition);
    WriteVector({_Ui->probeCenterX,_Ui->probeCenterY,_Ui->probeCenterZ},probe.Center);
    WriteVector({_Ui->probeSizeX,_Ui->probeSizeY,_Ui->probeSizeZ},probe.Size);
    _Ui->probeBlend->setText(InspectorNumbers::Format(probe.BlendDistance));
    _Ui->probeIntensity->setText(InspectorNumbers::Format(probe.Intensity));
    _Ui->probePriority->setText(QString::number(probe.Priority));
    _Ui->probeMask->SetMask(probe.CullingMask);

    const auto indirect=_IndirectIndex>=0?settings.IndirectLightVolumes[_IndirectIndex]:BakedIndirectVolume{};
    syncAsset(_Ui->indirectAsset,_IndirectIndex,_IndirectContext,indirect.Asset);
    WriteVector({_Ui->indirectCenterX,_Ui->indirectCenterY,_Ui->indirectCenterZ},indirect.Center);
    WriteVector({_Ui->indirectSizeX,_Ui->indirectSizeY,_Ui->indirectSizeZ},indirect.Size);
    _Ui->indirectBlend->setText(InspectorNumbers::Format(indirect.BlendDistance));
    _Ui->indirectIntensity->setText(InspectorNumbers::Format(indirect.Intensity));
    _Ui->indirectPriority->setValue(static_cast<int>(indirect.Priority));
}

bool SpatialLightingWindow::Commit(const EnvironmentLightingSettings& value) {
    if(!CanEdit())return false;
    std::string error;
    const bool accepted=_Authoring->SetEnvironment(_Snapshot.SceneID,value,error);
    SetStatus(accepted?QString{}:QString::fromStdString(error));
    return accepted;
}

void SpatialLightingWindow::ChangeVolume(VolumeKind kind,bool add) {
    if(!CanEdit())return;
    auto value=_Snapshot.Settings;
    const auto mutate=[&](auto& collection,int& index) {
        const int previous=index;
        if(add) {
            index=static_cast<int>(collection.size());
            collection.emplace_back();
        } else {
            if(index<0||index>=static_cast<int>(collection.size()))return;
            collection.erase(collection.begin()+index);
        }
        if(!Commit(value))index=previous;
    };
    if(kind==VolumeKind::Reflection)mutate(value.ReflectionProbes,_ProbeIndex);
    else if(!add||value.IndirectLightVolumes.size()<MaxBakedIndirectVolumes)
        mutate(value.IndirectLightVolumes,_IndirectIndex);
}

void SpatialLightingWindow::CommitPlanar() {
    if(!CanEdit())return;
    auto value=_Snapshot.Settings;
    auto& planar=value.PlanarReflection;
    planar.Enabled=_Ui->planarEnabled->isChecked();
    planar.MaxResolution=static_cast<std::uint32_t>(_Ui->planarResolution->value());
    planar.LayerMask=_Ui->planarMask->GetMask();
    if(!ReadFloat(_Ui->planarHeight,planar.Height)||!ReadFloat(_Ui->planarScale,planar.ResolutionScale)||
       !ReadFloat(_Ui->planarStrength,planar.Strength)||!ReadFloat(_Ui->planarDistance,planar.MaxDistance)||
       !IsValidPlanarReflection(planar)) {
        SetStatus(tr("平面高度须为有限数值，分辨率比例须在 0.125 到 1 之间，强度须在 0 到 1 之间，最大距离须大于零。"));
        return;
    }
    if(planar!=_Snapshot.Settings.PlanarReflection)Commit(value);
    else SetStatus({});
}

bool SpatialLightingWindow::CommitProbe() {
    if(!CanEdit()||_ProbeIndex<0)return false;
    auto value=_Snapshot.Settings;
    auto& probe=value.ReflectionProbes[_ProbeIndex];
    probe.Enabled=_Ui->probeEnabled->isChecked();
    probe.BoxProjection=_Ui->probeProjection->isChecked();
    probe.Environment=_Ui->probeAsset->GetValue();
    probe.CullingMask=_Ui->probeMask->GetMask();
    bool priorityOK=false;
    probe.Priority=_Ui->probePriority->text().trimmed().toInt(&priorityOK);
    if(!priorityOK||!ReadVector({_Ui->captureX,_Ui->captureY,_Ui->captureZ},probe.CapturePosition)||
       !ReadVector({_Ui->probeCenterX,_Ui->probeCenterY,_Ui->probeCenterZ},probe.Center)||
       !ReadVector({_Ui->probeSizeX,_Ui->probeSizeY,_Ui->probeSizeZ},probe.Size)||
       !ReadFloat(_Ui->probeBlend,probe.BlendDistance)||!ReadFloat(_Ui->probeIntensity,probe.Intensity)||
       !IsValidReflectionProbe(probe)) {
        SetStatus(tr("探针尺寸每轴须大于零，渐变距离须在零到最短边的一半之间，强度须为非负数，优先级须为有符号整数。"));
        return false;
    }
    if(probe!=_Snapshot.Settings.ReflectionProbes[_ProbeIndex])return Commit(value);
    SetStatus({});
    return true;
}

bool SpatialLightingWindow::CommitIndirect() {
    if(!CanEdit()||_IndirectIndex<0)return false;
    auto value=_Snapshot.Settings;
    auto& indirect=value.IndirectLightVolumes[_IndirectIndex];
    indirect.Asset=_Ui->indirectAsset->GetValue();
    indirect.Priority=static_cast<std::uint32_t>(_Ui->indirectPriority->value());
    if(!ReadVector({_Ui->indirectCenterX,_Ui->indirectCenterY,_Ui->indirectCenterZ},indirect.Center)||
       !ReadVector({_Ui->indirectSizeX,_Ui->indirectSizeY,_Ui->indirectSizeZ},indirect.Size)||
       !ReadFloat(_Ui->indirectBlend,indirect.BlendDistance)||!ReadFloat(_Ui->indirectIntensity,indirect.Intensity)||
       !IsValidBakedIndirectVolume(indirect)) {
        SetStatus(tr("体积尺寸每轴须大于零，渐变距离须为非负数，间接光强度须在 0 到 16 之间。"));
        return false;
    }
    if(indirect!=_Snapshot.Settings.IndirectLightVolumes[_IndirectIndex])return Commit(value);
    SetStatus({});
    return true;
}

void SpatialLightingWindow::BeginBake(VolumeKind kind) {
    if(!CanEdit()||!_Baker||_BakeState.IsActive())return;
    LightingBakeRequest request;
    request.SceneID=_Snapshot.SceneID;
    if(kind==VolumeKind::Reflection) {
        if(!CommitProbe())return;
        request.Kind=LightingBakeKind::ReflectionProbe;
        request.Index=static_cast<std::size_t>(_ProbeIndex);
        request.FaceSize=_Ui->probeBakeResolution->currentText().toUInt();
        request.ReflectionSize=_Ui->probeReflectionResolution->currentText().toUInt();
        request.ReflectionSamples=_Ui->probeSamples->currentText().toUInt();
        request.StaticOnly=_Ui->probeStaticOnly->isChecked();
        if(!ReadFloat(_Ui->probeCaptureDistance,request.CaptureDistance)||request.CaptureDistance<=0) {
            SetStatus(tr("探针捕获距离必须为有限正数。"));return;
        }
    } else {
        if(!CommitIndirect())return;
        request.Kind=LightingBakeKind::IndirectVolume;
        request.Index=static_cast<std::size_t>(_IndirectIndex);
        request.FaceSize=_Ui->indirectBakeResolution->currentText().toUInt();
        request.ReflectionSize=request.FaceSize;
        request.StaticOnly=_Ui->indirectStaticOnly->isChecked();
        request.Grid={static_cast<std::uint32_t>(_Ui->gridX->value()),static_cast<std::uint32_t>(_Ui->gridY->value()),
            static_cast<std::uint32_t>(_Ui->gridZ->value())};
        if(!ReadFloat(_Ui->indirectCaptureDistance,request.CaptureDistance)||request.CaptureDistance<=0) {
            SetStatus(tr("间接光捕获距离必须为有限正数。"));return;
        }
    }
    std::string error;
    if(!_Baker->BeginBake(request,error))SetStatus(QString::fromStdString(error));
}

void SpatialLightingWindow::RefreshBakeControls() {
    const bool ready=_Baker&&_Snapshot.Available&&_Snapshot.Editable&&!_BakeState.IsActive();
    _Ui->probeBakeGroup->setEnabled(ready&&_ProbeIndex>=0);
    _Ui->indirectBakeGroup->setEnabled(ready&&_IndirectIndex>=0);
    const bool cancel=_BakeState.Phase==LightingBakePhase::Capturing||_BakeState.Phase==LightingBakePhase::Filtering;
    _Ui->cancelBake->setVisible(cancel);
    _Ui->bakeProgress->setVisible(_BakeState.IsActive());
    if(_BakeState.Phase==LightingBakePhase::Capturing&&_BakeState.TotalFaces) {
        _Ui->bakeProgress->setRange(0,static_cast<int>(_BakeState.TotalFaces));
        _Ui->bakeProgress->setValue(static_cast<int>(_BakeState.CompletedFaces));
    } else if(_BakeState.IsActive())_Ui->bakeProgress->setRange(0,0);
    _Ui->bakeStatusLabel->setText(QString::fromStdString(_BakeState.Message));
    _Ui->bakeStatusLabel->setVisible(!_BakeState.Message.empty());
}

void SpatialLightingWindow::OnLightingBakeChanged(const LightingBakeState& state) {
    _BakeState=state;
    RefreshBakeControls();
}

void SpatialLightingWindow::SetStatus(const QString& status) {
    _Ui->statusLabel->setText(status);
    _Ui->statusLabel->setVisible(!status.isEmpty());
}
