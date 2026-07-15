#pragma once

#include "CoreMinimal.h"
#include "CortexTypes.h"

/**
 * Performance sampling / assertion ops for the QA domain.
 *
 * All three ops run a timed, per-frame sampling window and therefore return a DEFERRED result —
 * the real payload arrives via the DeferredCallback once the window closes. They gate on an active
 * PIE world (like every other QA op) and abort with PIE_TERMINATED if PIE ends mid-window.
 *
 * Sampling is per-frame (via FTSTicker, which receives the real frame DeltaTime) rather than a poll
 * of the smoothed GAverageFPS global, so min / p95 reflect actual frame variance.
 */
class FCortexQAPerfOps
{
public:
    /** Sample FPS/frametime over a window. Params: duration (s, default 3, clamped 0.5..60). */
    static FCortexCommandResult SamplePerformance(const TSharedPtr<FJsonObject>& Params, FDeferredResponseCallback DeferredCallback);

    /** Assert average FPS over the window >= min_fps (required). Optional: duration. */
    static FCortexCommandResult AssertFps(const TSharedPtr<FJsonObject>& Params, FDeferredResponseCallback DeferredCallback);

    /** Assert p95 frametime over the window <= max_ms (required). Optional: duration. */
    static FCortexCommandResult AssertFrametime(const TSharedPtr<FJsonObject>& Params, FDeferredResponseCallback DeferredCallback);
};
