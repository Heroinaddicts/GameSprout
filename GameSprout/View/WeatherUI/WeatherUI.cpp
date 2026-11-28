#include "WeatherUI.h"
#include "IIDEWindow.h"
#include "ui_WeatherUI.h"
#include <QApplication>
#include <QFileDialog>
#include <QSignalBlocker>

using namespace GameSproutRuntime;

namespace {
WeatherUI* ActiveWeatherUI=nullptr;

struct CloudControls {
    QDoubleSpinBox* Coverage;
    QDoubleSpinBox* Density;
    QDoubleSpinBox* Scale;
    QDoubleSpinBox* WindDirection;
    QDoubleSpinBox* WindSpeed;

    void Display(const WeatherCloudLayer& layer) const {
        Coverage->setValue(layer.Coverage);Density->setValue(layer.Density);Scale->setValue(layer.Scale);
        WindDirection->setValue(layer.WindDirectionDegrees);WindSpeed->setValue(layer.WindSpeed);
    }

    WeatherCloudLayer Read() const {
        return {float(Coverage->value()),float(Density->value()),float(Scale->value()),
            float(WindDirection->value()),float(WindSpeed->value())};
    }
};

CloudControls Cloud(const Ui::WeatherWindow& ui,std::size_t index) {
    if(index==0)return {ui.cloudACoverage,ui.cloudADensity,ui.cloudAScale,ui.cloudAWindDirection,ui.cloudAWindSpeed};
    return {ui.cloudBCoverage,ui.cloudBDensity,ui.cloudBScale,ui.cloudBWindDirection,ui.cloudBWindSpeed};
}
}

WeatherUI::~WeatherUI(){Release(nullptr);}

void WeatherUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* api) {
    if(!api)return;
    _Authoring=api->GetGameSproutModule<IWeatherAuthoring>("WeatherAuthoring");
    _IDE=api->GetGameSproutView<IIDEWindow>("IDEWindow");
}

bool WeatherUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Window=new QDialog;
    _UI=new Ui::WeatherWindow;
    _UI->setupUi(_Window);
    _UI->errorLabel->hide();
    QObject::connect(_UI->applyButton,&QPushButton::clicked,_Window,[this] {
        std::string error;
        _Authoring->ApplySettings(_Snapshot.SceneID,ReadControls(),error);ShowError(error);
    });
    QObject::connect(_UI->applyLookButton,&QPushButton::clicked,_Window,[this] {
        std::string error;
        _Authoring->ApplyLook(_Snapshot.SceneID,static_cast<WeatherLook>(_UI->lookCombo->currentIndex()),error);
        ShowError(error);
    });
    QObject::connect(_UI->previewLookButton,&QPushButton::clicked,_Window,[this] {
        std::string error;
        _Authoring->PreviewLook(_Snapshot.SceneID,static_cast<WeatherLook>(_UI->lookCombo->currentIndex()),
            float(_UI->transitionSeconds->value()),error);
        ShowError(error);
    });
    QObject::connect(_UI->previewTimeButton,&QPushButton::clicked,_Window,[this] {
        std::string error;
        _Authoring->PreviewTime(_Snapshot.SceneID,float(_UI->previewHour->value()),error);ShowError(error);
    });
    QObject::connect(_UI->previewPlayButton,&QPushButton::toggled,_Window,[this](bool playing) {
        std::string error;
        if(!_Authoring->SetPreviewPlaying(_Snapshot.SceneID,playing,error)) {
            const QSignalBlocker blocker(_UI->previewPlayButton);
            _UI->previewPlayButton->setChecked(_Snapshot.PreviewPlaying);
        }
        ShowError(error);
    });
    QObject::connect(_UI->resetPreviewButton,&QPushButton::clicked,_Window,[this] {
        std::string error;
        _Authoring->ResetPreview(_Snapshot.SceneID,error);ShowError(error);
    });
    QObject::connect(_UI->lightningButton,&QPushButton::clicked,_Window,[this] {
        std::string error;
        _Authoring->TriggerLightning(_Snapshot.SceneID,ReadControls().Lightning,error);ShowError(error);
    });
    QObject::connect(_UI->importButton,&QPushButton::clicked,_Window,[this]{PickProfile(false);});
    QObject::connect(_UI->exportButton,&QPushButton::clicked,_Window,[this]{PickProfile(true);});
    QObject::connect(_Window,&QDialog::finished,_Window,[this]{StopPreview();});
    return true;
}

bool WeatherUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Authoring||!_IDE||!_Window)return false;
    auto* menuBar=_IDE->GetMenuBar();
    auto* window=menuBar?menuBar->CreateChildMenu(_UI->windowAction->text()):nullptr;
    _Menu=window?window->CreateChildMenu(_UI->weatherAction->text()):nullptr;
    if(!_Menu)return false;
    ActiveWeatherUI=this;
    _Menu->SetClickCallback(&ShowMenu);
    _Menu->SetEnabled(true);
    _Subscribed=_Authoring->RegisterWeatherListener(this);
    WeatherAuthoringSnapshot snapshot;std::string error;
    if(!_Subscribed||!_Authoring->ReadWeather(snapshot,error))return false;
    OnWeatherAuthoringChanged(snapshot);
    return true;
}

void WeatherUI::ShowMenu(){if(ActiveWeatherUI)ActiveWeatherUI->Show();}
void WeatherUI::Show() {
    if(!_Window)return;
    _Window->show();_Window->raise();_Window->activateWindow();
}
void WeatherUI::Hide(){if(_Window){StopPreview();_Window->hide();}}

void WeatherUI::StopPreview() {
    if(!_Authoring||!_Snapshot.PreviewPlaying)return;
    std::string error;
    _Authoring->SetPreviewPlaying(_Snapshot.SceneID,false,error);
    ShowError(error);
}

void WeatherUI::PickProfile(bool save) {
    const auto sceneID=_Snapshot.SceneID;
    QFileDialog dialog(_Window,save?_UI->exportButton->text():_UI->importButton->text());
    dialog.setNameFilter(QStringLiteral("天气预设 (*.weather)"));
    dialog.setAcceptMode(save?QFileDialog::AcceptSave:QFileDialog::AcceptOpen);
    dialog.setFileMode(save?QFileDialog::AnyFile:QFileDialog::ExistingFile);
    if(save)dialog.setDefaultSuffix(QStringLiteral("weather"));
    if(dialog.exec()!=QDialog::Accepted||dialog.selectedFiles().isEmpty())return;
    const auto path=dialog.selectedFiles().front().toStdString();
    std::string error;
    if(save)_Authoring->ExportProfile(sceneID,path,error);
    else _Authoring->ImportProfile(sceneID,path,error);
    ShowError(error);
}

WeatherSettings WeatherUI::ReadControls() const {
    auto result=_Snapshot.Settings;
    result.Enabled=_UI->enabledCheck->isChecked();
    result.TimeOfDayHours=float(_UI->timeOfDay->value());
    result.DayLengthSeconds=float(_UI->dayLength->value());
    result.Preset.StarIntensity=float(_UI->starIntensity->value());
    result.Preset.StarDensity=float(_UI->starDensity->value());
    result.Preset.StarRotationDegrees=float(_UI->starRotation->value());
    result.Preset.DaylightMultiplier=float(_UI->dayLight->value());
    result.Preset.AmbientMultiplier=float(_UI->ambientLight->value());
    result.Preset.FogStrength=float(_UI->fogStrength->value());
    result.Preset.CloudColor={float(_UI->cloudRed->value()),float(_UI->cloudGreen->value()),
        float(_UI->cloudBlue->value()),result.Preset.CloudColor.A};
    for(std::size_t index=0;index<result.Preset.Clouds.size();++index)result.Preset.Clouds[index]=Cloud(*_UI,index).Read();
    result.Lightning.Position={float(_UI->strikeX->value()),float(_UI->strikeY->value()),float(_UI->strikeZ->value())};
    result.Lightning.Height=float(_UI->strikeHeight->value());
    result.Lightning.Intensity=float(_UI->strikeIntensity->value());
    result.Lightning.DurationSeconds=float(_UI->strikeDuration->value());
    result.Lightning.Radius=float(_UI->strikeRadius->value());
    result.Lightning.Seed=static_cast<std::uint32_t>(_UI->strikeSeed->value());
    result.Lightning.Tint={float(_UI->strikeRed->value()),float(_UI->strikeGreen->value()),
        float(_UI->strikeBlue->value()),result.Lightning.Tint.A};
    return result;
}

void WeatherUI::OnWeatherAuthoringChanged(const WeatherAuthoringSnapshot& snapshot) {
    const bool changed=snapshot.SceneID!=_Snapshot.SceneID||snapshot.Settings!=_Snapshot.Settings;
    _Snapshot=snapshot;
    if(changed) {
        const auto& value=snapshot.Settings;
        const auto& preset=value.Preset;
        const auto& lightning=value.Lightning;
        _UI->enabledCheck->setChecked(value.Enabled);
        _UI->timeOfDay->setValue(value.TimeOfDayHours);_UI->dayLength->setValue(value.DayLengthSeconds);
        _UI->starIntensity->setValue(preset.StarIntensity);_UI->starDensity->setValue(preset.StarDensity);
        _UI->starRotation->setValue(preset.StarRotationDegrees);_UI->fogStrength->setValue(preset.FogStrength);
        _UI->dayLight->setValue(preset.DaylightMultiplier);_UI->ambientLight->setValue(preset.AmbientMultiplier);
        _UI->cloudRed->setValue(preset.CloudColor.R);_UI->cloudGreen->setValue(preset.CloudColor.G);
        _UI->cloudBlue->setValue(preset.CloudColor.B);
        for(std::size_t index=0;index<preset.Clouds.size();++index)Cloud(*_UI,index).Display(preset.Clouds[index]);
        _UI->strikeX->setValue(lightning.Position.X);_UI->strikeY->setValue(lightning.Position.Y);
        _UI->strikeZ->setValue(lightning.Position.Z);_UI->strikeHeight->setValue(lightning.Height);
        _UI->strikeIntensity->setValue(lightning.Intensity);_UI->strikeDuration->setValue(lightning.DurationSeconds);
        _UI->strikeRadius->setValue(lightning.Radius);_UI->strikeSeed->setValue(lightning.Seed);
        _UI->strikeRed->setValue(lightning.Tint.R);_UI->strikeGreen->setValue(lightning.Tint.G);_UI->strikeBlue->setValue(lightning.Tint.B);
    }
    const QSignalBlocker blocker(_UI->previewPlayButton);
    _UI->previewPlayButton->setChecked(snapshot.PreviewPlaying);
    _UI->enabledCheck->setEnabled(snapshot.Editable);
    _UI->settingsTabs->setEnabled(snapshot.Editable);
    _UI->applyButton->setEnabled(snapshot.Editable);
    _UI->applyLookButton->setEnabled(snapshot.Editable);
    _UI->importButton->setEnabled(snapshot.Editable);
    _UI->exportButton->setEnabled(snapshot.SceneID!=0);
    _UI->previewLookButton->setEnabled(snapshot.PreviewAvailable);
    _UI->previewTimeButton->setEnabled(snapshot.PreviewAvailable);
    _UI->resetPreviewButton->setEnabled(snapshot.PreviewAvailable);
    _UI->lightningButton->setEnabled(snapshot.PreviewAvailable);
    _UI->previewPlayButton->setEnabled(snapshot.Editable&&snapshot.PreviewAvailable);
}

void WeatherUI::ShowError(const std::string& error) {
    _UI->errorLabel->setText(QString::fromStdString(error));
    _UI->errorLabel->setVisible(!error.empty());
}

void WeatherUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_Menu){_Menu->SetClickCallback(nullptr);_Menu->SetEnabled(false);}
    _Menu=nullptr;
    if(_Subscribed&&_Authoring)_Authoring->UnregisterWeatherListener(this);
    _Subscribed=false;
    StopPreview();
    delete _Window;_Window=nullptr;
    delete _UI;_UI=nullptr;
    _Authoring=nullptr;_IDE=nullptr;
    if(ActiveWeatherUI==this)ActiveWeatherUI=nullptr;
}
