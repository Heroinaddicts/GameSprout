#include "GRHIExport.h"
#include "GRHIFinal.h"

#if defined(GAMESPROUT_GRHI_STATIC_LINK)
GRHI::GRHIApi* GRHI::CreateGRHI() noexcept {
#else
extern "C" API GRHI::GRHIApi* CALL CreateFoundation() noexcept {
#endif
    try {
        return GRHI::GRHIFinal::GetInstance();
    } catch (...) {
        return nullptr;
    }
}
