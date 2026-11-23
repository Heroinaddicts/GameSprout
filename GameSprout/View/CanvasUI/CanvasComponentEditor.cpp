#include "CanvasComponentEditor.h"
#include "ui_CanvasComponentEditor.h"
#include <QLineEdit>

using GameSproutRuntime::CanvasPhysicalUnit;
using GameSproutRuntime::CanvasRenderMode;
using GameSproutRuntime::CanvasScaleMode;
using GameSproutRuntime::CanvasScreenMatchMode;
using GameSproutRuntime::ICanvasComponent;

CanvasComponentEditor::CanvasComponentEditor(QWidget* parent) : ComponentEditor(parent), _Ui(new Ui::CanvasComponentEditorForm()) {
    _Ui->setupUi(this);
    for (auto* edit : {_Ui->canvasResolutionXEdit, _Ui->canvasResolutionYEdit, _Ui->canvasPixelsPerUnitEdit,
                       _Ui->canvasScaleFactorEdit, _Ui->canvasWidthHeightMatchEdit, _Ui->canvasFallbackDpiEdit})
        connect(edit, &QLineEdit::editingFinished, this, [this,edit]{ CommitValues(edit); });
    for (auto* combo : {_Ui->canvasModeCombo, _Ui->canvasScaleModeCombo, _Ui->canvasMatchModeCombo, _Ui->canvasPhysicalUnitCombo})
        connect(combo, qOverload<int>(&QComboBox::activated), this, [this,combo]{ CommitValues(combo); });
    connect(_Ui->canvasUseSafeAreaCheck, &QCheckBox::clicked, this, [this]{ CommitValues(_Ui->canvasUseSafeAreaCheck); });
}
CanvasComponentEditor::~CanvasComponentEditor() { delete _Ui; }
void CanvasComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    const auto value = InspectorValues::Read(static_cast<ICanvasComponent&>(component));
    ShowNumbers({_Ui->canvasResolutionXEdit, _Ui->canvasResolutionYEdit, _Ui->canvasPixelsPerUnitEdit},
                {value.Resolution.X, value.Resolution.Y, value.Pixels});
    ShowNumbers({_Ui->canvasScaleFactorEdit, _Ui->canvasWidthHeightMatchEdit, _Ui->canvasFallbackDpiEdit},
                {value.Scale.ScaleFactor, value.Scale.WidthHeightMatch, value.Scale.FallbackDpi});
    _Ui->canvasModeCombo->setCurrentIndex(static_cast<int>(value.Mode));
    _Ui->canvasScaleModeCombo->setCurrentIndex(static_cast<int>(value.Scale.Mode));
    _Ui->canvasMatchModeCombo->setCurrentIndex(static_cast<int>(value.Scale.MatchMode));
    _Ui->canvasPhysicalUnitCombo->setCurrentIndex(static_cast<int>(value.Scale.PhysicalUnit));
    _Ui->canvasUseSafeAreaCheck->setChecked(value.Scale.UseSafeArea);

    const bool screen = value.Mode == CanvasRenderMode::ScreenSpace;
    const bool scaleWithScreen = screen && value.Scale.Mode == CanvasScaleMode::ScaleWithScreenSize;
    const bool fixedPixels = screen && value.Scale.Mode == CanvasScaleMode::ConstantPixelSize;
    const bool fixedPhysicalSize = screen && value.Scale.Mode == CanvasScaleMode::ConstantPhysicalSize;
    const auto showRow = [](QWidget* label, QWidget* field, bool visible) {
        label->setVisible(visible);
        field->setVisible(visible);
    };
    showRow(_Ui->canvasScaleModeLabel, _Ui->canvasScaleModeCombo, screen);
    showRow(_Ui->canvasResolutionLabel, _Ui->canvasResolutionFields, scaleWithScreen || !screen);
    showRow(_Ui->canvasMatchModeLabel, _Ui->canvasMatchModeCombo, scaleWithScreen);
    showRow(_Ui->canvasWidthHeightMatchLabel, _Ui->canvasWidthHeightMatchEdit,
            scaleWithScreen && value.Scale.MatchMode == CanvasScreenMatchMode::MatchWidthOrHeight);
    showRow(_Ui->canvasScaleFactorLabel, _Ui->canvasScaleFactorEdit, fixedPixels);
    showRow(_Ui->canvasPhysicalUnitLabel, _Ui->canvasPhysicalUnitCombo, fixedPhysicalSize);
    showRow(_Ui->canvasFallbackDpiLabel, _Ui->canvasFallbackDpiEdit, fixedPhysicalSize);
    showRow(_Ui->canvasPixelsPerUnitLabel, _Ui->canvasPixelsPerUnitEdit, !screen);
    _Ui->canvasUseSafeAreaCheck->setVisible(screen);
}
void CanvasComponentEditor::CommitValues(QWidget* changed) {
    if (_Loading || !_PropertiesEditable) return;
    auto* component = dynamic_cast<ICanvasComponent*>(_Context.ResolveComponent());
    if (!component) {
        Fail("Canvas 已失效。");
        return;
    }
    auto value = InspectorValues::Read(*component);
    if (changed == _Ui->canvasModeCombo) value.Mode = static_cast<CanvasRenderMode>(_Ui->canvasModeCombo->currentIndex());
    if (changed == _Ui->canvasScaleModeCombo) value.Scale.Mode = static_cast<CanvasScaleMode>(_Ui->canvasScaleModeCombo->currentIndex());
    if (changed == _Ui->canvasMatchModeCombo) value.Scale.MatchMode = static_cast<CanvasScreenMatchMode>(_Ui->canvasMatchModeCombo->currentIndex());
    if (changed == _Ui->canvasPhysicalUnitCombo) value.Scale.PhysicalUnit = static_cast<CanvasPhysicalUnit>(_Ui->canvasPhysicalUnitCombo->currentIndex());
    if (changed == _Ui->canvasUseSafeAreaCheck) value.Scale.UseSafeArea = _Ui->canvasUseSafeAreaCheck->isChecked();
    const std::pair<QLineEdit*,float*> fields[] {
        {_Ui->canvasResolutionXEdit, &value.Resolution.X},
        {_Ui->canvasResolutionYEdit, &value.Resolution.Y},
        {_Ui->canvasPixelsPerUnitEdit, &value.Pixels},
        {_Ui->canvasScaleFactorEdit, &value.Scale.ScaleFactor},
        {_Ui->canvasWidthHeightMatchEdit, &value.Scale.WidthHeightMatch},
        {_Ui->canvasFallbackDpiEdit, &value.Scale.FallbackDpi}
    };
    for (const auto& [field, number] : fields)
        if (changed == field && !ReadNumber(field, *number)) {
            Fail("请输入有限数字，可使用科学计数法。");
            return;
        }
    Commit<ICanvasComponent>(value, "修改 Canvas");
}
