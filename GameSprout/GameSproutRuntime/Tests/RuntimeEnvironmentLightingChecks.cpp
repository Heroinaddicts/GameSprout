#include "GameSproutRuntimeExport.h"
#include "SceneDocument.h"
#include "PrefabDocument.h"
#include "RuntimeWeatherApi.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
class Listener final:public GameSproutRuntime::IRuntimeListener {
public:
    unsigned Changes=0;std::uint64_t SceneID=0,ObjectID=0;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override {
        if(event.Type==GameSproutRuntime::SceneNodeEventType::EnvironmentLightingChanged) {
            ++Changes;SceneID=event.SceneID;ObjectID=event.ObjectID;
        }
    }
};
}
int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Runtime directory required");runtime=Export(argv[1]);Check(runtime,"Runtime load failed");
        auto* scene=runtime->CreateScene("Environment");auto* other=runtime->CreateScene("Isolated");
        Check(scene&&other&&scene->GetEnvironmentLighting().Mode==GameSproutRuntime::AmbientLightMode::None,"Environment must default to None");
        runtime->ProcessPendingEvents();Listener listener;Check(runtime->RegisterListener(&listener),"Environment event registration failed");
        auto settings=scene->GetEnvironmentLighting();settings.Mode=GameSproutRuntime::AmbientLightMode::SphericalHarmonics;
        settings.AmbientColor={4,.5f,.25f,1};settings.Intensity=2.5f;
        settings.Atmosphere.Enabled=true;settings.Atmosphere.DrawSky=false;
        settings.Atmosphere.ZenithColor={.1f,.3f,.7f,1};settings.Atmosphere.FogColor={.4f,.6f,.8f,1};
        settings.Atmosphere.FogStart=80;settings.Atmosphere.FogEnd=700;settings.Atmosphere.FogMaxOpacity=.7f;
        settings.SkyEnvironment={"abcdef1234567890abcdef1234567890",7};
        settings.SkyRotationDegrees=-35;settings.SkyIntensity=1.5f;settings.ReflectionIntensity=.25f;
        settings.HDREnabled=true;settings.ExposureEV=-1.5f;settings.Tonemap=GameSproutRuntime::ToneMapping::ACES;
        settings.SHCoefficients[0]={.2f,.3f,.4f};settings.SHCoefficients[6]={-.5f,.125f,-.25f};
        Check(scene->SetEnvironmentLighting(settings),"Signed SH and linear HDR must be valid");runtime->ProcessPendingEvents();
        Check(listener.Changes==1&&listener.SceneID==scene->GetInstanceID()&&listener.ObjectID==scene->GetInstanceID(),"Wrong environment root event identity");
        Check(other->GetEnvironmentLighting().Mode==GameSproutRuntime::AmbientLightMode::None,"Environment leaked to another scene");
        Check(scene->SetEnvironmentLighting(settings),"Unchanged valid environment rejected");runtime->ProcessPendingEvents();
        Check(listener.Changes==1,"Unchanged environment emitted duplicate event");
        settings.Atmosphere.FogStart=90;
        Check(scene->SetEnvironmentLighting(settings),"Atmosphere-only edit rejected");runtime->ProcessPendingEvents();
        Check(listener.Changes==2&&scene->GetEnvironmentLighting().Atmosphere.FogStart==90,"Atmosphere-only edit did not publish an environment event");
        settings.SpotShadows={3,512};
        Check(scene->SetEnvironmentLighting(settings),"Shadow-budget-only edit rejected");runtime->ProcessPendingEvents();
        Check(listener.Changes==3&&scene->GetEnvironmentLighting().SpotShadows==settings.SpotShadows&&
            other->GetEnvironmentLighting().SpotShadows==GameSproutRuntime::SpotShadowBudgetSettings{},
            "Spot shadow budget did not publish its own scene event");
        const auto invalid=[&](GameSproutRuntime::EnvironmentLightingSettings value) {
            Check(!scene->SetEnvironmentLighting(value)&&scene->GetEnvironmentLighting().Mode==settings.Mode&&
                scene->GetEnvironmentLighting().Intensity==settings.Intensity&&scene->GetEnvironmentLighting().AmbientColor.R==4&&
                scene->GetEnvironmentLighting().SHCoefficients[6].X==-.5f,"Invalid environment was not atomically rejected");
        };
        auto badSettings=settings;badSettings.Mode=static_cast<GameSproutRuntime::AmbientLightMode>(255);invalid(badSettings);
        badSettings=settings;badSettings.Intensity=-1;invalid(badSettings);
        badSettings=settings;badSettings.AmbientColor.G=-.1f;invalid(badSettings);
        badSettings=settings;badSettings.SkyColor.B=std::numeric_limits<float>::infinity();invalid(badSettings);
        badSettings=settings;badSettings.SHCoefficients[8].X=std::numeric_limits<float>::quiet_NaN();invalid(badSettings);
        badSettings=settings;badSettings.Atmosphere.FogStart=-1;invalid(badSettings);
        badSettings=settings;badSettings.Atmosphere.FogEnd=settings.Atmosphere.FogStart;invalid(badSettings);
        badSettings=settings;badSettings.Atmosphere.FogMaxOpacity=1.1f;invalid(badSettings);
        badSettings=settings;badSettings.Atmosphere.ZenithColor.R=std::numeric_limits<float>::infinity();invalid(badSettings);
        badSettings=settings;badSettings.SpotShadows.MaxLights=5;invalid(badSettings);
        badSettings=settings;badSettings.SpotShadows.MaxResolution=768;invalid(badSettings);
        runtime->ProcessPendingEvents();Check(listener.Changes==3,"Rejected environment emitted a notification");runtime->UnregisterListener(&listener);

        settings.PlanarReflection={true,-2.75f,.5f,768,.8f,150,7};
        auto& probe=settings.ReflectionProbes.emplace_back();
        probe.Environment={"abcdef1234567890abcdef1234567890",9};
        probe.Center={1,2,3};probe.CapturePosition={1,2,4};probe.Size={14,5,6};probe.Priority=-3;
        auto& volume=settings.IndirectLightVolumes.emplace_back();
        volume.Asset={"12121212121212121212121212121212",1};volume.Center={4,3,2};volume.Priority=8;
        Check(scene->SetEnvironmentLighting(settings),"Spatial lighting settings rejected");
        auto* object=scene->CreateGameObject("Prefab object");Check(object,"Cannot create ordinary object");
        SceneDocument::ObjectIDs ids;Json::Value document;std::string error;
        Check(SceneDocument::Serialize(*scene,ids,document,error),error);
        Check(document["EnvironmentLighting"]["Mode"]=="SphericalHarmonics"&&
            document["EnvironmentLighting"]["SHCoefficients"].size()==9&&
            document["EnvironmentLighting"]["SHCoefficients"][6][0].asFloat()==-.5f,"Scene lost the signed SH source data");
        Check(document["EnvironmentLighting"]["Atmosphere"]["Enabled"].asBool()&&
            !document["EnvironmentLighting"]["Atmosphere"]["DrawSky"].asBool()&&
            document["EnvironmentLighting"]["Atmosphere"]["FogStart"].asFloat()==90,"Scene lost its atmosphere settings");
        const auto& environment=document["EnvironmentLighting"];
        Check(environment["SkyEnvironment"]["Guid"]==settings.SkyEnvironment.Guid&&
            environment["SkyEnvironment"]["LocalID"].asUInt64()==7&&environment["SkyRotationDegrees"].asFloat()==-35&&
            environment["SkyIntensity"].asFloat()==1.5f&&environment["ReflectionIntensity"].asFloat()==.25f&&
            environment["HDREnabled"].asBool()&&environment["ExposureEV"].asFloat()==-1.5f&&environment["Tonemap"]=="ACES",
            "Scene lost skybox, reflection or HDR settings");
        Check(environment["SpotShadows"]["MaxLights"].asUInt()==3&&environment["SpotShadows"]["MaxResolution"].asUInt()==512,
            "Scene lost the authored spot shadow budget");
        GameSproutRuntime::IScene* loaded=nullptr;SceneDocument::ObjectIDs loadedIDs;
        Check(SceneDocument::Instantiate(document,*runtime,loaded,loadedIDs,error),error);
        Json::Value again;Check(SceneDocument::Serialize(*loaded,loadedIDs,again,error)&&again==document,"Environment scene round trip changed data");
        const auto reject=[&](const Json::Value& bad) {
            auto* preserved=loaded;const auto count=runtime->GetSceneCount();
            Check(!SceneDocument::Validate(bad,error)&&!SceneDocument::Instantiate(bad,*runtime,preserved,error)&&
                preserved==loaded&&runtime->GetSceneCount()==count,"Invalid environment partially instantiated a scene");
        };
        auto bad=document;bad["EnvironmentLighting"]["Exposure"]=1;reject(bad);
        bad=document;bad["EnvironmentLighting"]["Mode"]="Skybox";reject(bad);
        bad=document;bad["EnvironmentLighting"]["SHCoefficients"]=Json::Value(Json::arrayValue);reject(bad);
        bad=document;bad["EnvironmentLighting"]["SHCoefficients"][4][0]=std::numeric_limits<double>::infinity();reject(bad);
        bad=document;bad["EnvironmentLighting"]["GroundColor"][0]=-1;reject(bad);
        bad=document;bad["EnvironmentLighting"]["Atmosphere"]["Unknown"]=true;reject(bad);
        bad=document;bad["EnvironmentLighting"]["Atmosphere"]["Enabled"]=1;reject(bad);
        bad=document;bad["EnvironmentLighting"]["Atmosphere"]["FogStart"]=1000;reject(bad);
        bad=document;bad["EnvironmentLighting"]["Atmosphere"]["FogMaxOpacity"]=1.1;reject(bad);
        bad=document;bad["EnvironmentLighting"]["Atmosphere"]["FogColor"][0]=-1;reject(bad);
        bad=document;bad["EnvironmentLighting"]["SkyEnvironment"]["LocalID"]=0;reject(bad);
        bad=document;bad["EnvironmentLighting"]["SkyEnvironment"]["Guid"]="bad";reject(bad);
        bad=document;bad["EnvironmentLighting"]["SkyIntensity"]=-1;reject(bad);
        bad=document;bad["EnvironmentLighting"]["ReflectionIntensity"]=-1;reject(bad);
        bad=document;bad["EnvironmentLighting"]["HDREnabled"]=1;reject(bad);
        bad=document;bad["EnvironmentLighting"]["ExposureEV"]=17;reject(bad);
        bad=document;bad["EnvironmentLighting"]["Tonemap"]="Uncharted2";reject(bad);
        bad=document;bad["EnvironmentLighting"]["SpotShadows"]["MaxLights"]=3.5f;reject(bad);
        bad=document;bad["EnvironmentLighting"]["SpotShadows"]["MaxResolution"]=8192;reject(bad);
        bad=document;bad["EnvironmentLighting"]["PlanarReflection"]["ResolutionScale"]=2;reject(bad);
        bad=document;bad["EnvironmentLighting"]["ReflectionProbes"][0]["Size"][2]=0;reject(bad);
        bad=document;bad["EnvironmentLighting"]["ReflectionProbes"][0]["BoxProjection"]=1;reject(bad);
        bad=document;bad["EnvironmentLighting"]["IndirectLightVolumes"][0]["Priority"]=256;reject(bad);
        bad=document;bad["EnvironmentLighting"]=Json::Value();reject(bad);
        auto skyOnly=document;skyOnly["EnvironmentLighting"]=Json::Value(Json::objectValue);
        skyOnly["EnvironmentLighting"]["Mode"]="SkyEnvironment";
        Check(SceneDocument::Validate(skyOnly,error),"Scene cannot select skybox mode before binding a resource");
        bad=document;bad.removeMember("EnvironmentLighting");GameSproutRuntime::IScene* omitted=nullptr;
        Check(SceneDocument::Instantiate(bad,*runtime,omitted,error)&&omitted->GetEnvironmentLighting().Mode==GameSproutRuntime::AmbientLightMode::None,
            "Omitted environment must use the normal None default");

        auto prefab=document;prefab.removeMember("RootComponents");
        Check(!PrefabDocument::Validate(prefab,error),"Prefab accepted a scene environment");
        prefab.removeMember("EnvironmentLighting");std::vector<std::uint8_t> bytes;
        Check(PrefabDocument::Encode(prefab,bytes,error),error);
        GameSproutRuntime::PrefabData data;data.Bytes=bytes;std::vector<GameSproutRuntime::PrefabObjectMapping> mappings;
        Check(runtime->InstantiatePrefabData(scene,data,nullptr,mappings,error),error);
        Check(scene->GetEnvironmentLighting().Mode==settings.Mode&&scene->GetEnvironmentLighting().Intensity==2.5f,
            "Prefab instantiation overwrote its destination environment");

        auto* weather=GameSproutRuntime::QueryWeatherRuntime(*runtime);
        Check(weather,"Runtime weather interface unavailable");
        GameSproutRuntime::WeatherSettings weatherSettings;
        Check(weather->GetWeatherSettings(*omitted,weatherSettings)&&!weatherSettings.Enabled&&
            !document.isMember("Weather"),"Old scenes must retain the default weather state");
        weatherSettings.Enabled=true;
        weatherSettings.TimeOfDayHours=22.5f;
        weatherSettings.DayLengthSeconds=150;
        weatherSettings.Preset.Clouds[0].Coverage=.45f;
        weatherSettings.Preset.Clouds[1].WindDirectionDegrees=115;
        weatherSettings.Preset.StarDensity=.82f;
        weatherSettings.Lightning.Position={4,3,-2};
        weatherSettings.Lightning.Seed=std::numeric_limits<std::uint32_t>::max();
        Check(weather->SetWeatherSettings(*loaded,weatherSettings),"Cannot configure scene weather");
        Check(weather->SetTimeOfDay(*loaded,8),"Cannot preview independent scene time");
        auto preview=weatherSettings.Preset;preview.Clouds[0].Coverage=1;
        Check(weather->TransitionWeather(*loaded,preview,0),"Cannot preview independent weather preset");
        Json::Value weatherDocument,properties;
        Check(SceneDocument::Serialize(*loaded,loadedIDs,weatherDocument,error),error);
        Check(SceneDocument::SerializeProperties(*loaded,loadedIDs,properties,error)&&
            SceneDocument::ValidateProperties(properties,true,error)&&properties["Weather"]==weatherDocument["Weather"],
            "Incremental scene snapshot lost authored weather: "+error);
        GameSproutRuntime::IScene* weatherScene=nullptr;
        Check(SceneDocument::Instantiate(weatherDocument,*runtime,weatherScene,error),error);
        GameSproutRuntime::WeatherSettings roundTrip;
        Check(weather->GetWeatherSettings(*weatherScene,roundTrip)&&roundTrip==weatherSettings,
            "Scene failed to round trip authored weather or persisted transient preview state");
        bad=weatherDocument;bad["Weather"]["Preset"]["Clouds"][1]["Density"]=2;reject(bad);
        bad=weatherDocument;bad["Weather"]["Lightning"]["Seed"]=Json::UInt64(1ull<<32);reject(bad);
        prefab["Weather"]=weatherDocument["Weather"];
        Check(!PrefabDocument::Validate(prefab,error),"Prefab accepted scene weather");
        Check(runtime->DestroyScene(weatherScene),"Cannot release weather fixture");
        Check(runtime->DestroyScene(omitted)&&runtime->DestroyScene(loaded)&&runtime->DestroyScene(other)&&runtime->DestroyScene(scene),"Cannot release environment fixtures");
        delete runtime;std::cout<<"PASS environment and weather scene isolation, validation, persistence and Prefab exclusion\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';delete runtime;return 1;}
}
