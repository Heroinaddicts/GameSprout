#pragma once
#include "Api/RuntimeRenderApi.h"
#include "RuntimeShaders.h"
#include "RuntimeRenderResourceLease.h"
#include "RuntimeFrustum.h"
#include "GRHIApi.h"
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace GameSproutRuntime {
class Runtime;
class RenderResourceCache;
namespace RuntimeShadowMath { struct CascadeSet; }
namespace RenderEnvironment { struct Uniform; }
class Renderer final : public IRuntimeViewport {
public:
    Renderer(Runtime& runtime,GRHI::IDevice& device,IRuntimeResourceProvider& resources);
    ~Renderer() override;
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    bool Render(const RenderView& view) override;
    bool ReadPixels(std::vector<std::uint8_t>&,std::string&) override;
    bool ReadLinearColorHalf(std::vector<std::uint8_t>&,std::string&) override;
    bool Resize(std::uint32_t,std::uint32_t,std::string&) override;
    void BindSurface(GRHI::ISurface* surface) {_Surface=surface;}

    const std::string& GetLastError() const override {return _Error;}
    const RenderStatistics& GetStatistics() const override {return _Statistics;}
    bool GetRenderedCamera(RenderedCamera& camera) const override;
    bool Raycast(IScene* scene,const Ray& worldRay,SceneRaycastHit& hit,std::uint32_t cullingMask=~0u) override;
    bool GetWorldBounds(IScene* scene,std::uint64_t objectID,Bounds& bounds) override;
    bool AppendSelectionOutline(IScene* scene,std::uint64_t objectID,DebugDrawList& draw,
        const Color& tint,bool depthTest) override;
    bool AppendColliderWireframes(IScene* scene,const ColliderDebugOptions& options,DebugDrawList& draw) override;
private:
    friend class RenderResourceCache;
    struct ResourceKey {
        std::string Guid;std::uint64_t LocalID=0;
        explicit ResourceKey(const ResourceReference& reference):Guid(reference.Guid),LocalID(reference.LocalID) {}
        bool operator<(const ResourceKey& other) const {return std::tie(Guid,LocalID)<std::tie(other.Guid,other.LocalID);}
    };
    struct MeshGPU {
        std::uint64_t ResourceRevision=0;
        std::uint64_t SortRank=0;
        struct SkinVertex { std::uint32_t Bones[4]{}; float Weights[4]{}; };
        struct SkinInfluenceBounds {Vector3 Minimum,Maximum;bool Used=false;};
        MeshRenderData Data;
        std::vector<Vector3> Positions;
        std::vector<SkinVertex> Skin;
        std::vector<SkinInfluenceBounds> JointBounds;
        double MinimumSkinWeightSum=1.0,MaximumSkinWeightSum=1.0;
        std::vector<std::uint32_t> Indices;
        GRHI::IBuffer* Buffer=nullptr;
        GRHI::IBuffer* ShaderVertexBuffer=nullptr; // Canonical P/N/C/UV, only allocated for user shaders.
        // A private render-only bound derived from validated vertex positions.
        // Does not replace the provider's authored/queried LocalBounds contract.
        Vector3 CullMinimum,CullMaximum;
        bool CullBoundsKnown=false;
        Vector3 ReferencedMinimum,ReferencedMaximum;
        bool ReferencedBoundsKnown=false;
        std::string Error;
    };
    struct TextureGPU {std::uint64_t ResourceRevision=0;GRHI::ITexture* Texture=nullptr;GRHI::ISampler* Sampler=nullptr;bool SRGB=false;std::string Error;};
    struct EnvironmentGPU {
        std::uint64_t ResourceRevision=0;
        std::unique_ptr<GRHI::ITexture> Sky,Specular;
        std::array<Vector3,9> DiffuseSH{};
        float SpecularMaxLOD=0;
        bool SkyAttempted=false;
        std::string Error;
    };
    struct ColliderMeshWire {
        ResourceReference Reference;
        bool Convex=false;
        Mat4 Relative;
        std::vector<std::pair<Vector3,Vector3>> Edges;
        std::string Error;
    };
    struct BoundUniform {std::uint32_t Slot=0;std::vector<std::uint8_t> Bytes;};
    struct Material {
        std::uint64_t ResourceRevision=0;
        std::uint64_t SortRank=0;
        MaterialRenderData Data;bool Loaded=false;std::string Error;
        bool ShaderAttempted=false,ShaderReady=false;
        std::vector<BoundUniform> VertexUniforms,FragmentUniforms;
        std::vector<TextureGPU*> ShaderTextures;
        TextureGPU* BaseColorTexture=nullptr;
        TextureGPU* NormalTexture=nullptr;
        bool BaseColorAttempted=false,NormalAttempted=false;
        bool TexturePrefetchConsidered=false;
        std::string BaseColorError,NormalError;
    };
    struct ShaderGPU {
        std::uint64_t ResourceRevision=0;
        ShaderRenderData Data;
        GRHI::IShader* Vertex=nullptr;
        GRHI::IShader* Fragment=nullptr;
        bool Loaded=false;
        std::string Error;
    };
    struct ShaderPipelineKey {
        std::string Guid;std::uint64_t LocalID=0;
        GRHI::TextureFormat Format=GRHI::TextureFormat::Unknown;
        bool Negative=false;
        bool operator<(const ShaderPipelineKey& other) const {
            return std::tie(Guid,LocalID,Format,Negative)<std::tie(other.Guid,other.LocalID,other.Format,other.Negative);
        }
    };
    struct PipelineKey {
        GRHI::TextureFormat Format=GRHI::TextureFormat::Unknown;
        std::uint32_t Stride=0,Position=0,Normal=0;
        std::int32_t Color=-1,UV=-1,BoneIndices=-1,BoneWeights=-1;
        bool Grid=false,DoubleSided=false,Negative=false,DebugTriangles=false,DepthTest=true,Lit=false,Instanced=false,Shadow=false;
        bool EnvironmentReflections=false;
        bool SpatialLighting=false;
        bool operator<(const PipelineKey& other) const {
            return std::tie(Format,Stride,Position,Normal,Color,UV,BoneIndices,BoneWeights,Grid,DoubleSided,Negative,DebugTriangles,DepthTest,Lit,Instanced,Shadow,EnvironmentReflections,SpatialLighting)<
                std::tie(other.Format,other.Stride,other.Position,other.Normal,other.Color,other.UV,other.BoneIndices,other.BoneWeights,other.Grid,other.DoubleSided,other.Negative,other.DebugTriangles,other.DepthTest,other.Lit,other.Instanced,other.Shadow,other.EnvironmentReflections,other.SpatialLighting);
        }
    };
    struct Depth {GRHI::ITexture* Texture=nullptr;std::uint64_t LastUsed=0;};
    static constexpr std::size_t MaxLocalLights=4;
    struct DrawObject {
        std::uint64_t ObjectID=0;
        std::uint32_t InstanceIndex=0;
        Mat4 Model,Normal;
        ResourceReference Mesh;
        std::vector<ResourceReference> Materials;
        // Renderer-owned resource pointers, invalidated with the resource
        // cached resource generation. No Runtime node pointers survive between frames.
        MeshGPU* Geometry=nullptr;
        std::vector<Material*> MaterialCache;
        bool BoundsInitialized=false;
        bool CullBoxKnown=false;
        RenderFrustum::Box CullBox;
        std::string Name;
        std::vector<Mat4> SkinMatrices;
        bool Skinned=false;
        bool SkinPoseReady=false;
        bool StaticShadowCaster=false;
        ShadowCastingMode ShadowMode=ShadowCastingMode::On;
        bool ReceiveShadows=true;
        bool Negative=false;
        std::uint8_t Layer=0;
        std::uint64_t LocalLightGeneration=0,LocalLightFrame=0;
        std::uint64_t ReflectionProbeFrame=0,ReflectionProbeGeneration=0;
        std::size_t ReflectionProbeIndex=0;
        std::array<std::uint32_t,MaxLocalLights> LocalLightIndices{};
    };
    struct SkinnedDrawState {std::size_t ObjectIndex;Bounds LocalBounds;};
    struct DrawUniform {Mat4 Model,ViewProjection,Normal;Color BaseColor;Vector4 TextureST{1,1,0,0};};
    struct FrameUniform {Vector4 Time,CameraPosition,MainLightDirection,MainLightColor;};
    static_assert(sizeof(FrameUniform)==64);
    static constexpr std::size_t MaxDirectionalLights=4;
    static constexpr std::size_t MaxSpotShadows=4;
    struct DirectionalLight {
        Vector3 TowardsLight;
        Color Radiance;
        std::uint32_t CullingMask=0;
        LightShadowSettings Shadows;
        std::uint64_t ObjectID=0;
    };
    struct LocalLight {
        Vector3 Position,Forward;
        Color Radiance;
        LightType Type;
        std::uint32_t CullingMask;
        float Range,InnerCosine,OuterCosine,OuterAngle;
        double Luminance;
        LightShadowSettings Shadows;
        std::uint64_t ObjectID,ComponentID;
    };
    struct LitUniform {
        Vector4 CameraPosition;
        Vector4 Directions[MaxDirectionalLights]; // W is 1 for an active slot, 2 if it samples shadows.
        Color Radiances[MaxDirectionalLights];
        Vector4 Parameters; // Metallic, perceptual roughness, normal scale, has normal map.
        Color Emission;
        Vector4 NormalTextureST;
        Vector4 StyleParameters; // Strength, light threshold, softness, saturation.
        Color StylizedShadowTint;
        Vector4 LocalInfo; // X is the number of active slots.
        Vector4 LocalPositions[MaxLocalLights]; // XYZ position, W range.
        Vector4 LocalDirections[MaxLocalLights]; // XYZ spot forward, W outer cosine (-1 for Point).
        Vector4 LocalParameters[MaxLocalLights]; // X inner cosine, Y one-based shadow slot or zero.
        Color LocalRadiances[MaxLocalLights];
        Vector4 PlanarReflection; // Strength, reciprocal main width/height, plane height.
    };
    static_assert(sizeof(LitUniform)==512,"Lit material block must match HLSL packing");
    struct PreparedDraw;
    struct ShadowUniform {
        Mat4 WorldToShadow[4];
        Mat4 CameraView;
        Vector4 Splits;
        Vector4 Info; // Cascade count, strength, max distance, fade start.
        Vector4 Bias[4]; // Static depth bias, world normal bias, soft PCF, dynamic atlas valid (first entry).
        Vector4 Atlas[4]; // UV scale XY and top-first tile origin ZW.
        Vector4 DynamicDepth[4]; // Clip-space Z row per cascade. XY matches the static atlas.
        Vector4 DynamicDepthBias;
        Vector4 StaticScroll[4]; // Wrapped tile-local offset for the static layer.
    };
    static_assert(sizeof(ShadowUniform)==624,"Shadow fragment block must match HLSL packing");
    struct SpotShadowUniform {
        Mat4 WorldToShadow[MaxSpotShadows];
        Vector4 Atlas[MaxSpotShadows]; // UV scale XY, top-first tile origin ZW.
        Vector4 Parameters[MaxSpotShadows]; // Strength, depth bias, normal bias, filter tier.
        Vector4 Distance[MaxSpotShadows]; // Camera distance and fade start in metres, atlas texel XY.
    };
    static_assert(sizeof(SpotShadowUniform)==448,"Spot shadow block must match HLSL packing");
    bool RenderShadows(GRHI::ICommandBuffer&,const RenderView&,float aspect);
    struct InstanceDraw {
        std::size_t First;
        std::uint32_t Count;
        std::size_t IndexOffset;
    };
    struct InstanceStream {
        GRHI::IBuffer* Buffer=nullptr;
        std::vector<std::uint32_t> Indices;
        std::vector<std::uint32_t> Uploaded;
    };
    bool DrawShadow(GRHI::IRenderPass&,const PreparedDraw&,const Mat4&,
        std::size_t firstInstance=0,std::uint32_t instanceCount=1,const InstanceStream* instances=nullptr);
    struct PreparedDraw {
        const DrawObject* Object=nullptr;
        MeshGPU* Mesh=nullptr;
        MeshSection Section;
        const MaterialRenderData* Material=nullptr;
        const ResourceReference* MaterialReference=nullptr;
        TextureGPU* Texture=nullptr;
        TextureGPU* NormalTexture=nullptr;
        Renderer::Material* CachedMaterial=nullptr;
        ShaderGPU* Shader=nullptr;
        bool Transparent=false;
        float DistanceSquared=0;
        std::uint64_t SectionOrdinal=0;
        bool ShadowDoubleSided=false;
        std::array<std::uint32_t,MaxLocalLights> LocalLightIndices{}; // One-based indices, zero terminator.
        std::size_t ReflectionProbeIndex=0; // One-based authored index, zero selects the global sky.
    };
    struct ShadowCandidate { PreparedDraw Draw; Bounds World; };
    struct StaticShadowCasterKey {
        std::uint64_t ObjectID;
        const MeshGPU* Mesh;
        Mat4 Model;
        Bounds World;
        std::uint32_t FirstIndex,IndexCount;
        bool DoubleSided;
        bool operator==(const StaticShadowCasterKey&) const;
    };
    struct ShadowRegion {
        std::uint32_t Cascade;
        GRHI::Viewport Viewport;
        Mat4 Projection;
        std::uint32_t LogicalLeft,LogicalTop;
    };
    static void SortShadowCandidates(std::vector<ShadowCandidate>&);
    bool DrawShadowAtlas(GRHI::ICommandBuffer&,GRHI::ITexture&,const RenderView&,
        const RuntimeShadowMath::CascadeSet&,std::uint32_t tile,std::uint32_t columns,
        const std::vector<ShadowCandidate>&,InstanceStream&,std::uint32_t cascadeMask=0xF,
        const std::vector<ShadowRegion>* repair=nullptr);
    static bool BuildShadowRepairRegions(const Mat4*,const std::vector<Bounds>& removedBounds,
        const std::vector<Bounds>& addedBounds,const Vector4* scroll,std::uint32_t tile,std::uint32_t columns,
        std::uint32_t count,std::vector<ShadowRegion>&);
    static void AppendShadowRegion(std::vector<ShadowRegion>&,const Mat4&,std::uint32_t cascade,std::uint32_t tile,
        std::uint32_t columns,std::uint32_t left,std::uint32_t top,std::uint32_t width,std::uint32_t height,
        std::uint32_t physicalX,std::uint32_t physicalY);
    static bool AppendShadowScrollRegions(std::vector<ShadowRegion>&,std::uint32_t cascade,std::uint32_t tile,
        std::uint32_t columns,const Mat4& previous,const Mat4& current,Vector4& scroll);
    static void AppendWrappedShadowRegions(std::vector<ShadowRegion>&,const Mat4&,std::uint32_t cascade,
        std::uint32_t tile,std::uint32_t columns,int left,int top,int right,int bottom,int offsetX,int offsetY);
    GRHI::IGraphicsPipeline* PrepareShadowRepair();
    bool ClearShadowRegion(GRHI::IRenderPass&);
    GRHI::IBuffer* _ShadowRepairVertices=nullptr;
    struct InstanceVertex {Mat4 Model,Normal;};
    void RefreshDrawSortRanks();
    static bool OpaqueDrawBefore(const PreparedDraw&,const PreparedDraw&);
    static bool PriorityAndDepthBefore(const PreparedDraw&,const PreparedDraw&);
    void SortPrepared(std::vector<PreparedDraw>&);
    void PrepareInstanceBatches(const std::vector<PreparedDraw>&,bool enabled,bool shadow,
        std::vector<InstanceDraw>&,std::vector<std::uint32_t>&) const;
    bool UploadResidentInstances(GRHI::ICommandBuffer&);
    bool UploadInstanceStream(GRHI::ICommandBuffer&,InstanceStream&);
    bool PrepareColorInstances(GRHI::ICommandBuffer&,const RenderView&);
    bool RenderPlanarReflection(GRHI::ICommandBuffer&,const RenderView&,std::uint32_t width,std::uint32_t height,
        const RenderEnvironment::Uniform&,const EnvironmentGPU*,const EnvironmentGPU*);
    bool IsPlanarReceiver(const PreparedDraw&,const PlanarReflectionSettings&,Vector3 eye) const;
    bool UsesPlanarReflection(const PreparedDraw&) const;
    void BindPlanarReflection(GRHI::IRenderPass&,const PreparedDraw&,LitUniform&);
    bool DrawPrepared(GRHI::IRenderPass&,const std::vector<PreparedDraw>&,const RenderView&,const Mat4&,
        GRHI::TextureFormat,const std::vector<DirectionalLight>&,const RenderEnvironment::Uniform&,const EnvironmentGPU*);
    bool DrawBuiltin(GRHI::IRenderPass&,const PreparedDraw&,const RenderView&,const Mat4&,
        GRHI::TextureFormat,const std::vector<DirectionalLight>&,const RenderEnvironment::Uniform&,const EnvironmentGPU*,
        std::size_t firstInstance=0,std::uint32_t instanceCount=1);
    void InvalidateInstanceUploads();
    void ClearInstanceBuffers();
    bool ExtractLights(IScene*,std::vector<DirectionalLight>&);
    void PrepareLocalLights();
    void PrepareReflectionProbes(const RenderView&,const EnvironmentLightingSettings&);
    GRHI::ITexture* BlackEnvironmentCube();
    bool BindSpatialEnvironment(GRHI::IRenderPass&,const PreparedDraw&,const EnvironmentLightingSettings&,
        const EnvironmentGPU*,RenderEnvironment::Uniform&);
    LitUniform MakeLitUniform(const RenderView&,const PreparedDraw&,const std::vector<DirectionalLight>&);
    RenderFrustum::Box LocalLightObjectBounds(const DrawObject&,const MeshGPU&) const;
    bool RenderSpotShadows(GRHI::ICommandBuffer&,const RenderView&,const Mat4& viewProjection);
    // Keep this limit equal to Preview.hlsl. Uniform data is per object, never shared with the Mesh.
    static constexpr std::size_t MaxSkinBones = 128;
    struct SkinUniform { Mat4 Bones[MaxSkinBones]; };
    struct DebugVertex {Vector3 Position;Color Tint;};
    struct DebugBatch {std::vector<DebugVertex> Vertices;bool Triangles=false,DepthTest=false;};
    struct UIBatch {
        GRHI::ITexture* Texture;
        GRHI::ISampler* Sampler;
        std::uint32_t FirstVertex,VertexCount;
    };
    struct UIFont {
        FontRenderData Data;
        std::string Error;
        std::uint64_t ResourceRevision=0;
    };
    bool PrepareUI(IScene&,std::uint32_t width,std::uint32_t height,std::uint64_t previewObjectID,Vector2 gameViewportSize);
    bool DrawUI(GRHI::IRenderPass&,GRHI::TextureFormat color,GRHI::TextureFormat depth);
    bool DrawUIScreenDebug(GRHI::IRenderPass&,GRHI::TextureFormat color,GRHI::TextureFormat depth,
        Vector2 viewport,const UIDebugDrawList&,std::vector<GRHI::IBuffer*>& transientBuffers);
    bool InitializeSamples();
    bool InitializeUIFallback();
    const FontRenderData* UIFontData(const ResourceReference&);
    GRHI::IGraphicsPipeline* UIPipeline(GRHI::TextureFormat color,GRHI::TextureFormat depth);
    void ClearUIDraws();
    bool Initialize();
    bool Extract(const RenderView& view,std::vector<DrawObject>& objects);
    bool UpdateSceneTransforms(IScene& scene,const std::vector<std::uint64_t>& roots,std::vector<DrawObject>& objects);
    static bool SetDrawTransform(DrawObject& object,const Mat4& model);
    void PrepareDraws(const RenderView&,const Mat4& viewProjection);
    void RefreshSkinPoses();
    MeshGPU* Mesh(const ResourceReference& reference);
    ColliderMeshWire* ColliderMesh(IColliderComponent& collider,const ResourceReference& reference,bool convex,const Mat4& relative);
    TextureGPU* Texture(const ResourceReference& reference,bool* pending=nullptr);
    GRHI::ITexture* UploadPreparedTexture(const TextureRenderData&,std::string&);
    GRHI::ISampler* CreateTextureSampler(const SamplerSettings&);
    TextureGPU* MaterialTexture(Material&,bool normal,bool& pending);
    void BeginTexturePrefetchFrame(bool enabled);
    void EndTexturePrefetchFrame();
    bool PrefetchTexture(const ResourceReference&,bool visible);
    void PredictMaterialTextures(Material&);
    bool _TexturePrefetchEnabled=false;
    std::uint64_t _SeenTexturePrefetchRevision=0;
    Material* MaterialData(const ResourceReference& reference);
    ShaderGPU* Shader(const ResourceReference& reference);
    bool PrepareShaderMaterial(Material& material,ShaderGPU& shader);
    bool EnsureShaderVertices(MeshGPU& mesh,const ResourceReference& reference);
    GRHI::IGraphicsPipeline* ShaderPipeline(const ResourceReference&,ShaderGPU&,GRHI::TextureFormat,bool negative);
    bool DrawShaderMaterial(GRHI::IRenderPass&,Material&,ShaderGPU&,MeshGPU&,const ResourceReference&,
        const MeshSection&,const DrawObject&,const Mat4&,GRHI::TextureFormat);
    GRHI::IGraphicsPipeline* Pipeline(const PipelineKey& key);
    GRHI::ITexture* DepthTarget(std::uint32_t width,std::uint32_t height);
    GRHI::ITexture* HDRTarget(std::uint32_t width,std::uint32_t height);
    GRHI::IGraphicsPipeline* FullscreenPipeline(PreviewShader vertex,PreviewShader fragment,
        GRHI::TextureFormat color,GRHI::TextureFormat depth);
    bool ResolveHDR(GRHI::IRenderPass&,GRHI::TextureFormat color,GRHI::TextureFormat depth,const EnvironmentLightingSettings&);
    const EnvironmentGPU* Environment(const ResourceReference&,bool loadSky=true);
    GRHI::ISampler* EnvironmentSampler();
    bool DrawGrid(GRHI::IRenderPass& pass,const Mat4& viewProjection,GRHI::TextureFormat format);
    bool DrawSky(GRHI::IRenderPass&,const RenderView&,GRHI::TextureFormat,float aspect,
        const std::vector<DirectionalLight>&,const EnvironmentGPU*);
    bool PrepareDebug(const DebugDrawList* draw,std::vector<DebugBatch>& batches);
    bool DrawDebug(GRHI::IRenderPass& pass,const Mat4& viewProjection,GRHI::TextureFormat format,
                   const std::vector<DebugBatch>& batches,std::vector<GRHI::IBuffer*>& buffers);
    bool BeginQuery(IScene* scene);
    bool QueryObject(IScene* scene,std::uint64_t objectID,IGameObject*& object,MeshGPU*& mesh,Mat4& model);
    bool SkinMatrices(IGameObject& object,const MeshGPU& mesh,std::vector<Mat4>& matrices);
    bool SkinPoseBounds(const MeshGPU& mesh,const std::vector<Mat4>& matrices,Bounds& bounds);
    bool PosedPositions(IGameObject& object,const MeshGPU& mesh,std::vector<Vector3>& positions);
    bool PosedPositions(const MeshGPU& mesh,const std::vector<Mat4>& matrices,std::vector<Vector3>& positions);
    void SyncRevision();
    void ClearResources();
    void Diagnostic(const std::string& error);
    bool Fail(const std::string& error);
    std::string DeviceError(const std::string& operation) const;
    Runtime& _Runtime;
    GRHI::IDevice& _Device;
    GRHI::ISurface* _Surface=nullptr;
    GRHI::ITexture* _ColorTarget=nullptr;
    // An auxiliary color view owns visibility/batches and borrows this view's
    // prepared shadow atlases. It shares meshes, materials and pipelines.
    std::unique_ptr<Renderer> _PlanarCapture;
    std::unique_ptr<GRHI::ITexture> _PlanarColor;
    const Renderer* _PlanarShadowSource=nullptr;
    Vector4 _PlanarParameters;
    IRuntimeResourceProvider& _Resources;
    RenderResourceLease _ResourceLease;
    RenderResourceCache& _ResourceCache;
    std::map<ResourceKey,MeshGPU>& _Meshes;
    const MeshGPU* _OutlinedMesh=nullptr;
    std::vector<std::pair<std::uint32_t,std::uint32_t>> _OutlineEdges;
    std::map<std::pair<std::uint64_t,std::uint64_t>,ColliderMeshWire> _ColliderMeshes;
    std::map<ResourceKey,TextureGPU>& _Textures;
    std::map<ResourceKey,Material>& _Materials;
    std::map<ResourceKey,ShaderGPU>& _UserShaders;
    std::map<ShaderPipelineKey,GRHI::IGraphicsPipeline*>& _ShaderPipelines;
    std::map<PipelineKey,GRHI::IGraphicsPipeline*>& _Pipelines;
    std::vector<DrawObject> _SceneObjects;
    std::unordered_map<std::uint64_t,std::size_t> _SceneObjectIndices;
    std::vector<DirectionalLight> _SceneLights;
    std::vector<LocalLight> _LocalLights;
    std::vector<bool> _VisibleLocalShadowReceivers;
    std::uint64_t _LocalLightGeneration=1;
    bool _HadLocalLighting=false;
    struct ReflectionProbeBinding {const EnvironmentGPU* Resource=nullptr;bool Resolved=false;};
    std::vector<ReflectionProbeSettings> _ReflectionProbeSettings;
    std::vector<ReflectionProbeBinding> _ReflectionProbeBindings;
    std::uint64_t _ReflectionProbeGeneration=1,_ReflectionProbeEnvironmentGeneration=0;
    std::unordered_set<std::uint64_t> _LightTransformSources;
    FrameUniform _ShaderFrame;
    std::vector<PreparedDraw> _PreparedDraws;
    std::vector<PreparedDraw> _VisibleDrawScratch;
    std::vector<std::size_t> _OpaqueDrawOrder,_VisibleOpaqueSlots,_VisibleTransparentSlots;
    bool _OpaqueDrawOrderKnown=false;
    std::vector<SkinnedDrawState> _SkinnedDraws;
    std::vector<Mat4> _SkinScratch;
    Mat4 _PreparedViewProjection;
    RenderCamera _PreparedCamera;
    MaterialRenderData _DefaultMaterial;
    bool _PreparedKnown=false;
    std::uint32_t _PreparedVisible=0,_PreparedSkipped=0;
    std::string _PreparedDiagnostic;
    std::size_t _SortedMeshCount=0,_SortedMaterialCount=0;
    std::uint64_t _ObjectSceneID=0,_ObjectSceneRevision=0,_LightSceneID=0,_LightSceneRevision=0;
    std::uint32_t _ObjectCullingMask=0,_ExtractionSkipped=0;
    bool _ObjectSnapshotKnown=false,_LightSnapshotKnown=false;
    std::string _ExtractionDiagnostic,_LightDiagnostic;
    std::map<std::pair<std::uint32_t,std::uint32_t>,Depth> _Depths;
    std::unique_ptr<GRHI::ITexture> _HDRTarget;
    GRHI::IShader* (&_Shaders)[CachedPreviewShaderCount];
    GRHI::ITexture* _ShadowAtlas=nullptr;
    GRHI::ITexture* _DynamicShadowAtlas=nullptr;
    GRHI::ISampler* _ShadowSampler=nullptr;
    std::uint32_t _ShadowResolution=0;
    std::size_t _ShadowLightIndex=static_cast<std::size_t>(-1);
    ShadowUniform _ShadowUniform;
    RenderCamera _ShadowCamera;
    LightShadowSettings _ShadowSettings;
    Vector3 _ShadowDirection;
    std::uint32_t _ShadowLightMask=0;
    std::uint64_t _ShadowPreviewObjectID=0;
    std::vector<StaticShadowCasterKey> _StaticShadowCasters;
    std::vector<std::pair<std::uint64_t,Bounds>> _PrimaryShadowCasterBounds;
    std::unordered_set<std::uint64_t> _StaticShadowTransformObjects;
    std::vector<ShadowCandidate> _StaticShadowCandidates;
    std::vector<Bounds> _StaticShadowBounds;
    std::vector<DrawObject*> _DynamicShadowObjects;
    float _ShadowAspect=0;
    std::uint64_t _ShadowSceneID=0,_ShadowSceneRevision=0;
    std::uint64_t _ShadowLightObjectID=0,_ShadowLightComponentID=0,_ShadowRefreshRevision=0;
    bool _ShadowValid=false,_ShadowRecorded=false,_ShadowNavigationApproximate=false;
    GRHI::ITexture* _SpotShadowAtlas=nullptr;
    SpotShadowUniform _SpotShadowUniform;
    struct SpotShadowCacheKey {
        std::uint64_t ObjectID=0,ComponentID=0,Refresh=0;
        std::uint32_t Edge=0,CullingMask=0;
        Mat4 Projection;
    };
    std::array<SpotShadowCacheKey,MaxSpotShadows> _SpotShadowKeys{};
    std::array<std::uint32_t,MaxSpotShadows> _SpotShadowIndices{};
    std::array<std::vector<StaticShadowCasterKey>,MaxSpotShadows> _SpotShadowCasters;
    std::uint64_t _SpotShadowSceneID=0,_SpotShadowSceneRevision=0;
    std::uint32_t _SpotShadowViewMask=0,_SpotShadowTileStride=0;
    std::size_t _SpotShadowCount=0;
    bool _SpotShadowValid=false,_SpotShadowRecorded=false,_SpotShadowHasSkinned=false;
    GRHI::IBuffer* _InstanceTransformsBuffer=nullptr;
    std::vector<InstanceVertex> _InstanceTransforms;
    std::vector<std::uint32_t> _ChangedInstanceTransforms;
    std::vector<InstanceDraw> _ColorInstanceDraws;
    InstanceStream _ColorInstances,_StaticShadowInstances,_DynamicShadowInstances,_SpotShadowInstances;
    bool _InstanceTransformsDirty=true,_InstanceFrameSubmitted=true;
    bool _ColorInstanceBatchesKnown=false,_ColorInstancingEnabled=false;
    GRHI::ITexture*& _White;
    GRHI::ISampler*& _DefaultSampler;
    GRHI::IBuffer*& _Grid;
    std::uint32_t& _GridVertices;
    GRHI::IShader* _UIVertex=nullptr;
    GRHI::IShader* _UIFragment=nullptr;
    GRHI::ITexture* _UIFallbackAtlas=nullptr;
    FontRenderData _UIFallback;
    std::map<ResourceKey,UIFont> _UIFonts;
    std::map<std::pair<GRHI::TextureFormat,GRHI::TextureFormat>,GRHI::IGraphicsPipeline*> _UIPipelines;
    GRHI::IBuffer* _UIBuffer=nullptr;
    std::vector<UIBatch> _UIBatches;
    std::uint64_t _UISceneID=0,_UISceneRevision=0,_UIResourceRevision=0,_UIViewportRevision=0;
    std::uint64_t _UIPreviewObjectID=0;
    Vector2 _UIPreviewGameSize;
    std::uint32_t _UIWidth=0,_UIHeight=0;
    std::string _UIDiagnostic;
    std::uint64_t _ResourceGeneration=0,_Frame=0;
    bool _Rendering=false;
    RenderStatistics _Statistics;
    RenderedCamera _RenderedCamera;
    bool _HasRenderedCamera=false;
    bool _LinearCaptureReady=false;
    StaticFlags _ObjectRequiredStaticFlags=StaticFlags::None;
    std::string _Error;
};
}
