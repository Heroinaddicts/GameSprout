#include "EnvironmentUI.h"
#include "IIDEWindow.h"
#include "ui_EnvironmentUI.h"
#include <QApplication>
#include <QPushButton>
#include <QTableWidget>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace GameSproutRuntime;

namespace {
EnvironmentUI* ActiveEnvironmentUI=nullptr;

QTableWidgetItem* Cell(const QTableWidget* table,int row,int column) {
    return table->item(row,column);
}
QString Text(const QTableWidget* table,int row,int column) {
    const auto* item=Cell(table,row,column);
    return item?item->text().trimmed():QString{};
}
QString Number(float value) {
    return QString::number(value,'g',std::numeric_limits<float>::max_digits10);
}
void Append(QTableWidget* table,std::initializer_list<QString> columns) {
    const int row=table->rowCount();
    table->insertRow(row);
    int column=0;
    for(const auto& text:columns)table->setItem(row,column++,new QTableWidgetItem(text));
}
[[noreturn]] void Invalid(const QTableWidget* table,int row,int column,const QString& detail) {
    const auto title=table->horizontalHeaderItem(column)->text();
    throw std::runtime_error(QStringLiteral("第 %1 行「%2」%3").arg(row+1).arg(title,detail).toStdString());
}
int ReadInteger(const QTableWidget* table,int row,int column) {
    bool ok=false;
    const int result=Text(table,row,column).toInt(&ok);
    if(!ok)Invalid(table,row,column,QStringLiteral("请输入整数。"));
    return result;
}
float ReadFloat(const QTableWidget* table,int row,int column) {
    bool ok=false;
    const float result=Text(table,row,column).toFloat(&ok);
    if(!ok||!std::isfinite(result))Invalid(table,row,column,QStringLiteral("请输入有限数值。"));
    return result;
}
std::string ReadId(const QTableWidget* table,int row,int column) {
    const auto result=Text(table,row,column).toStdString();
    if(result.empty())Invalid(table,row,column,QStringLiteral("请输入 ID。"));
    return result;
}
bool ReadBool(const QTableWidget* table,int row,int column) {
    const auto text=Text(table,row,column);
    if(text==QStringLiteral("true"))return true;
    if(text==QStringLiteral("false"))return false;
    Invalid(table,row,column,QStringLiteral("请输入 true 或 false。"));
}
EnvironmentValue ReadValue(const IEnvironmentAuthoring& authoring,const QTableWidget* table,int row,int column) {
    EnvironmentValue value;
    std::string error;
    if(!authoring.ParseValue(Text(table,row,column).toStdString(),value,error))
        Invalid(table,row,column,QString::fromStdString(error));
    return value;
}
Vector3 ReadVector(const IEnvironmentAuthoring& authoring,const QTableWidget* table,int row,int column) {
    const auto value=ReadValue(authoring,table,row,column);
    if(const auto* vector=std::get_if<Vector3>(&value))return *vector;
    Invalid(table,row,column,QStringLiteral("请输入三维坐标，例如 [0,1,2]。"));
}
QString UniqueID(const QTableWidget* table,const QString& prefix) {
    QString id;
    bool occupied=false;
    int sequence=0;
    do {
        id=prefix+QString::number(++sequence);
        occupied=false;
        for(int row=0;row<table->rowCount()&&!occupied;++row)occupied=Text(table,row,0)==id;
    } while(occupied);
    return id;
}
QString CurrentID(const QTableWidget* table) {
    return Text(table,std::max(0,table->currentRow()),0);
}
void RemoveRows(QTableWidget* table,QTableWidget* children) {
    auto rows=table->selectionModel()->selectedRows();
    std::sort(rows.begin(),rows.end(),[](const auto& left,const auto& right){return left.row()>right.row();});
    for(const auto& row:rows) {
        const auto id=Text(table,row.row(),0);
        if(children)
            for(auto child=children->rowCount()-1;child>=0;--child)
                if(Text(children,child,0)==id)children->removeRow(child);
        table->removeRow(row.row());
    }
}
template<class T>
T& Owner(std::vector<T>& values,const QTableWidget* table,int row) {
    const auto id=ReadId(table,row,0);
    const auto result=std::find_if(values.begin(),values.end(),[&id](const auto& value){return value.Id==id;});
    if(result==values.end())Invalid(table,row,0,QStringLiteral("没有对应的层或曲线。"));
    return *result;
}
}

EnvironmentUI::~EnvironmentUI(){Release(nullptr);}

void EnvironmentUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* api) {
    if(!api)return;
    _Authoring=api->GetGameSproutModule<IEnvironmentAuthoring>("EnvironmentAuthoring");
    _IDE=api->GetGameSproutView<IIDEWindow>("IDEWindow");
}
bool EnvironmentUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Window=new QDialog;
    _UI=new Ui::EnvironmentWindow;
    _UI->setupUi(_Window);
    auto* layers=_UI->layersTable;
    auto* overrides=_UI->overridesTable;
    auto* curves=_UI->curvesTable;
    auto* keys=_UI->keysTable;
    auto* effects=_UI->effectsTable;

    ConnectTable(layers,_UI->layersAdd,_UI->layersRemove,[layers] {
        Append(layers,{UniqueID(layers,QStringLiteral("Layer")),QStringLiteral("0"),QStringLiteral("1"),{},{},{}});
    });
    ConnectTable(overrides,_UI->overridesAdd,_UI->overridesRemove,[overrides,layers] {
        Append(overrides,{CurrentID(layers),{},{},QStringLiteral("0")});
    });
    ConnectTable(curves,_UI->curvesAdd,_UI->curvesRemove,[curves] {
        Append(curves,{UniqueID(curves,QStringLiteral("Curve")),{},{},QStringLiteral("false"),QStringLiteral("0"),QStringLiteral("1")});
    });
    ConnectTable(keys,_UI->keysAdd,_UI->keysRemove,[keys,curves] {
        Append(keys,{CurrentID(curves),QStringLiteral("0"),QStringLiteral("0")});
    });
    ConnectTable(effects,_UI->effectsAdd,_UI->effectsRemove,[effects] {
        Append(effects,{UniqueID(effects,QStringLiteral("Effect")),{}});
    });
    QObject::connect(_UI->catalogRefresh,&QPushButton::clicked,_Window,[this]{RefreshCatalog();});
    QObject::connect(_UI->reloadButton,&QPushButton::clicked,_Window,[this]{LoadRows();});
    QObject::connect(_UI->applyButton,&QPushButton::clicked,_Window,[this]{Apply();});
    _UI->catalogTable->resizeColumnsToContents();
    return true;
}
void EnvironmentUI::ConnectTable(QTableWidget* table,QPushButton* add,QPushButton* remove,std::function<void()> addRow) {
    table->resizeColumnsToContents();
    QObject::connect(add,&QPushButton::clicked,_Window,[table,addRow=std::move(addRow)] {
        addRow();
        table->setCurrentCell(table->rowCount()-1,0);
        table->scrollToBottom();
    });
    QObject::connect(remove,&QPushButton::clicked,_Window,[this,table] {
        QTableWidget* children=nullptr;
        if(table==_UI->layersTable)children=_UI->overridesTable;
        if(table==_UI->curvesTable)children=_UI->keysTable;
        RemoveRows(table,children);
    });
}
bool EnvironmentUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Authoring||!_IDE||!_Window)return false;
    auto* menuBar=_IDE->GetMenuBar();
    auto* window=menuBar?menuBar->CreateChildMenu(_UI->windowAction->text()):nullptr;
    _Menu=window?window->CreateChildMenu(_UI->environmentAction->text()):nullptr;
    if(!_Menu)return false;
    ActiveEnvironmentUI=this;
    _Menu->SetClickCallback(&ShowMenu);
    _Menu->SetEnabled(true);
    QObject::connect(_Window,&QDialog::finished,_Window,[this] {
        if(_Subscribed)_Authoring->UnregisterEnvironmentListener(this);
        _Subscribed=false;
    });
    return true;
}
void EnvironmentUI::ShowMenu(){if(ActiveEnvironmentUI)ActiveEnvironmentUI->Show();}
void EnvironmentUI::Show() {
    if(!_Window)return;
    if(!_Subscribed)_Subscribed=_Authoring->RegisterEnvironmentListener(this);
    OnEnvironmentChanged(_Authoring->ReadEnvironment());
    RefreshCatalog();
    _Window->show();
    _Window->raise();
    _Window->activateWindow();
}
void EnvironmentUI::Hide() {
    if(!_Window)return;
    if(_Subscribed)_Authoring->UnregisterEnvironmentListener(this);
    _Subscribed=false;
    _Window->hide();
}
void EnvironmentUI::OnEnvironmentChanged(const EnvironmentAuthoringSnapshot& snapshot) {
    const bool reload=snapshot.SceneID!=_Snapshot.SceneID||snapshot.Settings!=_Snapshot.Settings;
    _Snapshot=snapshot;
    _UI->sceneState->setText(!snapshot.SceneID?QStringLiteral("请先打开场景。"):
        (snapshot.Editable?QStringLiteral("修改将在应用后写入当前场景。"):QStringLiteral("停止运行后可编辑。")));
    for(auto* page:{_UI->layersPage,_UI->overridesPage,_UI->curvesPage,_UI->keysPage,_UI->effectsPage})
        page->setEnabled(snapshot.Editable);
    _UI->applyButton->setEnabled(snapshot.Editable);
    _UI->reloadButton->setEnabled(snapshot.Editable);
    if(reload)LoadRows();
}
void EnvironmentUI::LoadRows() {
    auto* layers=_UI->layersTable;
    auto* overrides=_UI->overridesTable;
    auto* curves=_UI->curvesTable;
    auto* keys=_UI->keysTable;
    auto* effects=_UI->effectsTable;
    for(auto* table:{layers,overrides,curves,keys,effects})table->setRowCount(0);
    const auto format=[this](const EnvironmentValue& value){return QString::fromStdString(_Authoring->FormatValue(value));};
    for(const auto& layer:_Snapshot.Settings.Layers) {
        const auto id=QString::fromStdString(layer.Id);
        Append(layers,{id,QString::number(layer.Priority),Number(layer.Weight),
            layer.Region?format(layer.Region->Center):QString{},
            layer.Region?format(layer.Region->HalfSize):QString{},
            layer.Region?Number(layer.Region->BlendDistance):QString{}});
        for(const auto& data:layer.Overrides)
            Append(overrides,{id,QString::fromStdString(data.Target),QString::fromStdString(data.Parameter),format(data.Value)});
    }
    for(const auto& curve:_Snapshot.Settings.Curves) {
        const auto id=QString::fromStdString(curve.Id);
        Append(curves,{id,QString::fromStdString(curve.Target),QString::fromStdString(curve.Parameter),
            curve.Loop?QStringLiteral("true"):QStringLiteral("false"),QString::number(curve.Priority),Number(curve.Weight)});
        for(const auto& key:curve.Keys)Append(keys,{id,Number(key.Seconds),format(key.Value)});
    }
    for(const auto& effect:_Snapshot.Settings.Effects)
        Append(effects,{QString::fromStdString(effect.Id),QString::fromStdString(effect.Type)});
    _UI->statusLabel->clear();
}
GameSproutRuntime::EnvironmentSettings EnvironmentUI::ReadRows() const {
    EnvironmentSettings settings;
    const auto* layers=_UI->layersTable;
    const auto* overrides=_UI->overridesTable;
    const auto* curves=_UI->curvesTable;
    const auto* keys=_UI->keysTable;
    const auto* effects=_UI->effectsTable;
    for(int row=0;row<layers->rowCount();++row) {
        EnvironmentLayer layer;
        layer.Id=ReadId(layers,row,0);
        layer.Priority=ReadInteger(layers,row,1);
        layer.Weight=ReadFloat(layers,row,2);
        if(!Text(layers,row,3).isEmpty()||!Text(layers,row,4).isEmpty()||!Text(layers,row,5).isEmpty()) {
            const auto center=ReadVector(*_Authoring,layers,row,3);
            const auto halfSize=ReadVector(*_Authoring,layers,row,4);
            const auto blendDistance=Text(layers,row,5).isEmpty()?0.f:ReadFloat(layers,row,5);
            layer.Region=EnvironmentRegion{center,halfSize,blendDistance};
        }
        settings.Layers.push_back(std::move(layer));
    }
    for(int row=0;row<overrides->rowCount();++row)
        Owner(settings.Layers,overrides,row).Overrides.push_back({Text(overrides,row,1).toStdString(),
            ReadId(overrides,row,2),ReadValue(*_Authoring,overrides,row,3)});
    for(int row=0;row<curves->rowCount();++row)
        settings.Curves.push_back({ReadId(curves,row,0),Text(curves,row,1).toStdString(),
            ReadId(curves,row,2),{},ReadBool(curves,row,3),ReadInteger(curves,row,4),ReadFloat(curves,row,5)});
    for(int row=0;row<keys->rowCount();++row)
        Owner(settings.Curves,keys,row).Keys.push_back({ReadFloat(keys,row,1),ReadValue(*_Authoring,keys,row,2)});
    for(int row=0;row<effects->rowCount();++row)
        settings.Effects.push_back({ReadId(effects,row,0),ReadId(effects,row,1)});
    return settings;
}
void EnvironmentUI::Apply() {
    std::string error;
    try {
        const auto value=ReadRows();
        if(_Authoring->ApplySettings(_Snapshot.SceneID,value,error)) {
            OnEnvironmentChanged(_Authoring->ReadEnvironment());
            _UI->statusLabel->setText(QStringLiteral("已应用。请保存场景。"));
            return;
        }
    } catch(const std::exception& exception){error=exception.what();}
    _UI->statusLabel->setText(QString::fromStdString(error));
}
void EnvironmentUI::RefreshCatalog() {
    auto* table=_UI->catalogTable;
    table->setRowCount(0);
    const auto add=[this,table](const QString& type,const EnvironmentParameterDefinition& parameter) {
        Append(table,{type,QString::fromStdString(parameter.Id),QString::fromStdString(_Authoring->FormatValue(parameter.Default))});
    };
    for(const auto& parameter:_Authoring->ReadParameters())add({},parameter);
    for(const auto& effect:_Authoring->ReadEffects()) {
        const auto type=QString::fromStdString(effect.Type);
        if(effect.Parameters.empty())Append(table,{type,{},{}});
        for(const auto& parameter:effect.Parameters)add(type,parameter);
    }
    for(const auto& event:_Authoring->ReadEvents()) {
        const auto type=QString::fromStdString(event.Type)+QStringLiteral("（事件）");
        if(event.Parameters.empty())Append(table,{type,{},{}});
        for(const auto& parameter:event.Parameters)add(type,parameter);
    }
    table->resizeColumnsToContents();
}
void EnvironmentUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_Menu){_Menu->SetClickCallback(nullptr);_Menu->SetEnabled(false);}
    _Menu=nullptr;
    if(_Subscribed&&_Authoring)_Authoring->UnregisterEnvironmentListener(this);
    _Subscribed=false;
    if(ActiveEnvironmentUI==this)ActiveEnvironmentUI=nullptr;
    delete _Window;_Window=nullptr;
    delete _UI;_UI=nullptr;
    _Authoring=nullptr;_IDE=nullptr;
}
