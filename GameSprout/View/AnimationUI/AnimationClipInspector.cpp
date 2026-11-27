#include "AnimationClipInspector.h"
#include "AnimationClipOptionsEditor.h"
#include "ResourceField.h"
#include "ui_AnimationClipInspector.h"
#include <QSignalBlocker>

namespace {
GameSproutRuntime::ResourceReference ReadReference(const Json::Value& value) {
    if(value.isObject()&&value["Guid"].isString()&&value["LocalID"].isUInt64())
        return {value["Guid"].asString(),value["LocalID"].asUInt64()};
    return {};
}
Json::Value WriteReference(const GameSproutRuntime::ResourceReference& reference) {
    Json::Value value;
    value["Guid"]=reference.Guid;
    value["LocalID"]=Json::UInt64(reference.LocalID);
    return value;
}
}

AnimationClipInspector::AnimationClipInspector(QWidget* parent):ImportSettingsInspector(parent),_Ui(new Ui::AnimationClipInspectorForm()) {
    _Ui->setupUi(this);
    _Ui->sourceAvatarField->Changed=[this](const auto& reference) {
        if(!reference.Guid.empty())_Ui->unitySkeletonField->SetValue({});
        RefreshEnabled();
        if(Changed)Changed();
    };
    _Ui->unitySkeletonField->Changed=[this](const auto& reference) {
        if(!reference.Guid.empty())_Ui->sourceAvatarField->SetValue({});
        RefreshEnabled();
        if(Changed)Changed();
    };
    connect(_Ui->unityRootPathEdit,&QLineEdit::textEdited,this,[this]{if(Changed)Changed();});
    connect(_Ui->unityTransformOnlyCheck,&QCheckBox::toggled,this,[this]{if(Changed)Changed();});
    _Ui->clipOptionsEditor->Changed=[this]{if(Changed)Changed();};
    RefreshEnabled();
}
AnimationClipInspector::~AnimationClipInspector() {
    _Ui->sourceAvatarField->Changed={};
    _Ui->unitySkeletonField->Changed={};
    _Ui->clipOptionsEditor->Changed={};
    delete _Ui;
}
void AnimationClipInspector::BindSourceAvatar(IAssetsManager* assets,ILibraryManager* library,ISelectionManager* selection,const std::string& key) {
    _Ui->sourceAvatarField->Bind(assets,library,selection,eAssetType::Avatar);
    _Ui->sourceAvatarField->SetContextKey(key);
    _Ui->unitySkeletonField->Bind(assets,library,selection,eAssetType::Skeleton);
    _Ui->unitySkeletonField->SetContextKey(key+":UnitySkeleton");
}
void AnimationClipInspector::SetNodePaths(const std::vector<std::string>& paths){_Ui->clipOptionsEditor->SetNodePaths(paths);}
void AnimationClipInspector::RefreshEnabled() {
    const bool hasSkeleton=!_Ui->unitySkeletonField->GetValue().Guid.empty();
    _Ui->unityRootPathEdit->setEnabled(hasSkeleton);
    _Ui->unityTransformOnlyCheck->setEnabled(hasSkeleton);
}
void AnimationClipInspector::Load(const Json::Value& settings) {
    ImportSettingsInspector::Load(settings);
    _Ui->clipOptionsEditor->Load(settings["ClipOptions"]);
    _Ui->sourceAvatarField->SetValue(ReadReference(settings["SourceAvatar"]));
    const auto& unity=settings["UnityClip"];
    _Ui->unitySkeletonField->SetValue(ReadReference(unity["Skeleton"]));
    _Ui->unityRootPathEdit->setText(QString::fromStdString(unity.get("RootPath","").asString()));
    const QSignalBlocker blockTransformOnly(_Ui->unityTransformOnlyCheck);
    _Ui->unityTransformOnlyCheck->setChecked(unity.get("TransformOnly",false).asBool());
    RefreshEnabled();
}
bool AnimationClipInspector::Read(Json::Value& settings,std::string& error) const {
    if(!ImportSettingsInspector::Read(settings,error))return false;
    Json::Value options;if(!_Ui->clipOptionsEditor->Read(options,error))return false;
    if(settings.isMember("ClipOptions")||!options.empty())settings["ClipOptions"]=std::move(options);
    const auto avatar=_Ui->sourceAvatarField->GetValue();
    const auto skeleton=_Ui->unitySkeletonField->GetValue();
    if(!avatar.Guid.empty()&&!skeleton.Guid.empty()) {
        error="Unity 骨架与 Source Humanoid Avatar 只能选择一项。";
        return false;
    }
    if(avatar.Guid.empty())settings.removeMember("SourceAvatar");
    else settings["SourceAvatar"]=WriteReference(avatar);
    if(skeleton.Guid.empty())settings.removeMember("UnityClip");
    else {
        auto& unity=settings["UnityClip"];
        unity["Skeleton"]=WriteReference(skeleton);
        const auto root=_Ui->unityRootPathEdit->text().trimmed();
        if(root.isEmpty())unity.removeMember("RootPath");
        else unity["RootPath"]=root.toStdString();
        const bool transformOnly=_Ui->unityTransformOnlyCheck->isChecked();
        if(unity.isMember("TransformOnly")||transformOnly)unity["TransformOnly"]=transformOnly;
    }
    return true;
}
