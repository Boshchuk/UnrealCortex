#pragma once

namespace UE::CortexFrontend
{
inline bool ShouldTraverseToolMenusDuringShutdown(
    const bool bIsEngineExitRequested,
    const bool bIsToolMenusAvailable)
{
    return !bIsEngineExitRequested && bIsToolMenusAvailable;
}
}
