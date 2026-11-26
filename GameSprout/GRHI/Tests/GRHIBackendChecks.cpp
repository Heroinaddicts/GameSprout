#include "GRHIExport.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

#if defined(SDL_PLATFORM_WINDOWS)
#include "../GRHI/Renderer/TextureBlitDXIL.h"
#endif

namespace {
void Check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
#if defined(SDL_PLATFORM_WINDOWS)
void CheckWin32TextureSurface(GRHI::IDevice& device) {
    const std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)> window(
        CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"GRHI surface check",
            WS_POPUP | WS_VISIBLE, -10000, -10000, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr),
        DestroyWindow);
    Check(window != nullptr, "Cannot create the Win32 test window");
    const std::unique_ptr<GRHI::ISurface> surface(device.CreateSurface(window.get()));
    Check(surface != nullptr, device.GetLastError().c_str());

    auto* command = device.AcquireCommandBuffer();
    Check(command != nullptr, device.GetLastError().c_str());
    GRHI::SurfaceFrame frame;
    const auto acquired = surface->AcquireFrame(command, frame);
    const auto acquireError = device.GetLastError();
    const bool released = acquired == GRHI::AcquireFrameResult::Ready ? device.Submit(command) : device.Cancel(command);
    Check(acquired == GRHI::AcquireFrameResult::Ready, acquireError.empty() ? "Win32 swapchain is unavailable" : acquireError.c_str());
    Check(released && frame.Width && frame.Height, "Cannot submit the Win32 swapchain frame");

    GRHI::TextureDesc desc;
    desc.Width = desc.Height = 1;
    const uint8_t rgba[]{255, 0, 0, 255};
    const GRHI::TextureInitialData data{rgba, sizeof(rgba)};
    const std::unique_ptr<GRHI::ITexture> texture(device.CreateTexture(desc, &data));
    Check(texture != nullptr, device.GetLastError().c_str());
    const bool drawn = surface->Draw(texture.get());
    Check(drawn, device.GetLastError().c_str());

    // Read back a separate draw from the same shaders because swapchain
    // textures are presented on submission and cannot be read back by GRHI.
    GRHI::ShaderCode code;
    code.Format = GRHI::ShaderCodeFormat::DXIL;
    code.Data = GRHI::EmbeddedShaders::TextureBlitVertex;
    code.DataSize = sizeof(GRHI::EmbeddedShaders::TextureBlitVertex);
    code.EntryPoint = "imageVertex";
    const std::unique_ptr<GRHI::IShader> vertex(device.CreateShader({GRHI::ShaderStage::Vertex, &code, 1}));
    Check(vertex != nullptr, device.GetLastError().c_str());
    code.Data = GRHI::EmbeddedShaders::TextureBlitFragment;
    code.DataSize = sizeof(GRHI::EmbeddedShaders::TextureBlitFragment);
    code.EntryPoint = "imageFragment";
    code.SamplerCount = 1;
    const std::unique_ptr<GRHI::IShader> fragment(device.CreateShader({GRHI::ShaderStage::Fragment, &code, 1}));
    Check(fragment != nullptr, device.GetLastError().c_str());

    GRHI::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.VertexShader = vertex.get();
    pipelineDesc.FragmentShader = fragment.get();
    pipelineDesc.ColorFormat = desc.Format;
    const std::unique_ptr<GRHI::IGraphicsPipeline> pipeline(device.CreateGraphicsPipeline(pipelineDesc));
    Check(pipeline != nullptr, device.GetLastError().c_str());
    const std::unique_ptr<GRHI::ISampler> sampler(device.CreateSampler({}));
    Check(sampler != nullptr, device.GetLastError().c_str());
    desc.Usage = GRHI::TextureUsage::ColorTarget;
    const std::unique_ptr<GRHI::ITexture> target(device.CreateTexture(desc));
    Check(target != nullptr, device.GetLastError().c_str());

    command = device.AcquireCommandBuffer();
    Check(command != nullptr, device.GetLastError().c_str());
    GRHI::RenderPassDesc passDesc;
    passDesc.ColorTarget = target->GetColorTargetView();
    auto* pass = command->BeginRenderPass(passDesc);
    if (!pass) {
        const auto passError = device.GetLastError();
        device.Cancel(command);
        throw std::runtime_error(passError);
    }
    pass->SetPipeline(pipeline.get());
    pass->SetFragmentTextureSampler(0, texture.get(), sampler.get());
    pass->Draw(3);
    command->EndRenderPass(pass);
    const bool submitted = device.Submit(command);
    Check(submitted, device.GetLastError().c_str());
    std::vector<uint8_t> pixels;
    const bool read = device.ReadTexture(target.get(), pixels);
    Check(read, device.GetLastError().c_str());
    Check(pixels == std::vector<uint8_t>{255, 0, 0, 255}, "Direct3D 12 texture draw returned unexpected pixels");
}
#endif
}
int main(int argc,char** argv) {
    GRHI::GRHIApi* api=nullptr;
    GRHI::IDevice* device=nullptr;
    const char* previous=SDL_GetHint(SDL_HINT_GPU_DRIVER);
    const bool hadHint=previous!=nullptr;
    const std::string previousValue=previous?previous:"";
    const auto cleanup=[&] {
        delete device;device=nullptr;delete api;api=nullptr;
        if(hadHint)SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER,previousValue.c_str(),SDL_HINT_OVERRIDE);
        else SDL_ResetHint(SDL_HINT_GPU_DRIVER);
    };
    try {
        Check(argc==2,"Expected runtime directory");
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        const char* allowed="metal";const char* forbidden="vulkan";
#elif defined(SDL_PLATFORM_WINDOWS)
        const char* allowed="direct3d12";const char* forbidden="vulkan";
#else
        const char* allowed="vulkan";const char* forbidden="metal";
#endif
        Check(SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER,forbidden,SDL_HINT_OVERRIDE),"Cannot set test hint");
        api=ExportGRHIApi(argv[1]);Check(api,"Cannot load GRHI");
#if defined(SDL_PLATFORM_WINDOWS) && !defined(SDL_GRHI_GPU_ADAPTERS_VERSION)
        Check(api->GetAdapterList().empty(),"Stock SDL should not report explicitly selectable adapters");
#else
        Check(!api->GetAdapterList().empty(),"Backend override affected physical GPU enumeration");
#endif
        device=api->CreateDevice();Check(!device,"Forbidden backend override was accepted");
        Check(api->GetLastError().find("SDL_GPU_DRIVER")!=std::string::npos,"Missing platform-policy error");
#if !defined(SDL_PLATFORM_WINDOWS) || defined(SDL_GRHI_GPU_ADAPTERS_VERSION)
        device=api->CreateDevice(api->GetAdapterList().front().Id);Check(!device,"Explicit adapter bypassed backend policy");
#endif
        Check(SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER,allowed,SDL_HINT_OVERRIDE),"Cannot set allowed hint");
        device=api->CreateDevice();Check(device,"Allowed SDL backend could not create the device");
        delete device;device=nullptr;
#if defined(SDL_PLATFORM_WINDOWS) && !defined(SDL_GRHI_GPU_ADAPTERS_VERSION)
        device=api->CreateDevice("unavailable-explicit-adapter");
        Check(!device,"Stock SDL silently replaced the requested adapter with the default");
        Check(api->GetLastError().find("adapter extension")!=std::string::npos,"Missing explicit adapter error");
#endif
        SDL_ResetHint(SDL_HINT_GPU_DRIVER);
        device=api->CreateDevice();Check(device,"Default platform backend could not create the device");
#if defined(SDL_PLATFORM_WINDOWS)
        CheckWin32TextureSurface(*device);
        std::cout<<"PASS Win32 swapchain, DXIL texture draw and RGBA readback\n";
#endif
        cleanup();std::cout<<"PASS SDL GPU fixed platform backend, default/explicit adapter and forbidden override rejection\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';cleanup();return 1;}
}
