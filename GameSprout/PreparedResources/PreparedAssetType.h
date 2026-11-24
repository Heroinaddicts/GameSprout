#pragma once
#include <cstdint>

// 一个可独立引用的资源对象是什么，由 IAsset 及其具体资源接口使用。
// 资源对象通过源文件 GUID + LocalID 标识；主资源也有自己的对象类型。
// Model 表示模型层级及其资源引用描述，不表示该文件中所有对象都是 Model。
// 两个枚举各自独立，不能通过 static_cast 或数值相等来推断对应关系。
// 例如 Model 导入器可以产生 Model、Mesh、Material、AnimationClip、Avatar。
// Meta 与源文件工厂使用 eAssetImporterType，不使用本枚举判断后缀。
enum class eAssetType : uint16_t {
    Unknown = 0,

    // 几何、表面及其他基础资源。
    Texture = 1,
    Mesh = 3,
    Model = 4,
    Material = 5,
    Shader = 6,
    ComputeShader = 7,
    Font = 9,
    RenderTexture = 21,
    Cubemap = 22,
    SpriteAtlas = 23,
    Sprite = 32, // 图片的一个可独立引用区域，与 Texture 对象分开。

    // 动画和骨骼；一个 Model 可以包含多个动画片段。
    Skeleton = 30, // 骨骼层级、绑定姿态等骨架描述。
    Avatar = 31,   // 通用或人形动画使用的骨架映射描述。
    AnimationClip = 14,
    AnimatorController = 15,
    AnimatorOverrideController = 16,
    AvatarMask = 17,

    // 音视频和数据资源。
    Audio = 2, // 音频片段资源。
    AudioMixer = 18,
    Video = 10,
    Text = 11,
    Script = 12,
    DataAsset = 27,

    // 场景、物理、地形和光照相关资源。
    Scene = 13,
    Prefab = 255, // 可由Runtime装配的对象模板。
    PhysicsMaterial = 19,
    PhysicsMaterial2D = 20,
    TerrainData = 24,
    TerrainLayer = 25,
    ShaderVariantCollection = 26,
    LightingSettings = 28,
    LightmapParameters = 29,
    Environment = 33,
    Irradiance = 34,

    // 编辑器侧资源类别，不作为可执行 Shader 或游戏运行时目录产物。
    ShaderInclude = 8,
    Folder = 254
};

namespace PreparedResources {
// Coordinate-bearing resources use the current Unity world-space contract.
// This is a type-specific current format, not a fallback for old RH payloads.
inline constexpr uint32_t CurrentDataVersion(eAssetType type) {
    switch(type) {
    case eAssetType::Mesh:
        return 4;
    case eAssetType::Model:
    case eAssetType::Skeleton:
    case eAssetType::AnimationClip:
    case eAssetType::Avatar:
        return 3;
    default:
        return 2;
    }
}
}
