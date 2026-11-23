#ifndef __SurfaceSDL_h__
#define __SurfaceSDL_h__
#include "DeviceSDL.h"
namespace GRHI {
class TextureRendererSDL;
class SurfaceSDL final : public ISurface {
public:
    ~SurfaceSDL() override { Release(); }
    SurfaceSDL(const SurfaceSDL&) = delete;
    SurfaceSDL& operator=(const SurfaceSDL&) = delete;
    static SurfaceSDL* CreateSurfaceSDL(DeviceSDL*, GRHIWindowHandle);
    static SurfaceSDL* BorrowWindow(DeviceSDL*, SDL_Window*);
    void Release() override;
    TextureFormat GetColorFormat() const override;
    bool Draw(ITexture*, const Color&) override;
    AcquireFrameResult AcquireFrame(ICommandBuffer*, SurfaceFrame&) override;
    AcquireFrameResult TryAcquireFrame(ICommandBuffer*, SurfaceFrame&) override;
private:
    AcquireFrameResult AcquireFrame(ICommandBuffer*, SurfaceFrame&, bool waitForFrame);
    static SurfaceSDL* ClaimWindow(DeviceSDL*, SDL_Window*, GRHIWindowHandle, bool ownsWindow);
    SurfaceSDL(DeviceSDL* device, SDL_Window* window, GRHIWindowHandle handle, bool ownsWindow)
        : _Device(device), _Window(window), _HostHandle(handle), _OwnsWindow(ownsWindow) {}
    DeviceSDL* const _Device;
    SDL_Window* _Window;
    const GRHIWindowHandle _HostHandle;
    const bool _OwnsWindow;
    TextureRendererSDL* _Renderer = nullptr;
};
}
#endif
