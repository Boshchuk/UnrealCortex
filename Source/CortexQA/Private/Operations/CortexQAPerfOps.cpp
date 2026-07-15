#include "Operations/CortexQAPerfOps.h"

#include "CortexCommandRouter.h"
#include "CortexQAUtils.h"
#include "CortexTypes.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/World.h"

// Engine-maintained smoothed rolling average FPS (no public header declares it; the codebase
// forward-declares it locally — see CortexQAWorldOps.cpp). Reported as a reference alongside the
// per-frame-derived stats below.
extern ENGINE_API float GAverageFPS;

namespace
{
    /** Aggregate stats derived from a set of per-frame delta-seconds samples. */
    struct FCortexPerfStats
    {
        int32 Count = 0;
        double FpsMin = 0.0;
        double FpsAvg = 0.0;
        double FpsMax = 0.0;
        double MsAvg = 0.0;
        double MsP95 = 0.0;
        double MsMax = 0.0;
    };

    /** Nearest-rank percentile over an ascending-sorted array. */
    double Percentile(const TArray<double>& SortedAscending, double P)
    {
        const int32 Num = SortedAscending.Num();
        if (Num == 0)
        {
            return 0.0;
        }
        const int32 Index = FMath::Clamp(FMath::RoundToInt(P * (Num - 1)), 0, Num - 1);
        return SortedAscending[Index];
    }

    FCortexPerfStats ComputeStats(const TArray<double>& DeltaSeconds)
    {
        FCortexPerfStats Stats;
        Stats.Count = DeltaSeconds.Num();
        if (Stats.Count == 0)
        {
            return Stats;
        }

        TArray<double> FpsSamples;
        TArray<double> MsSamples;
        FpsSamples.Reserve(Stats.Count);
        MsSamples.Reserve(Stats.Count);

        double FpsSum = 0.0;
        double MsSum = 0.0;
        for (double Dt : DeltaSeconds)
        {
            const double Fps = Dt > 0.0 ? 1.0 / Dt : 0.0;
            const double Ms = Dt * 1000.0;
            FpsSamples.Add(Fps);
            MsSamples.Add(Ms);
            FpsSum += Fps;
            MsSum += Ms;
        }

        FpsSamples.Sort();
        MsSamples.Sort();

        Stats.FpsMin = FpsSamples[0];
        Stats.FpsMax = FpsSamples.Last();
        Stats.FpsAvg = FpsSum / Stats.Count;
        Stats.MsAvg = MsSum / Stats.Count;
        Stats.MsMax = MsSamples.Last();
        Stats.MsP95 = Percentile(MsSamples, 0.95);
        return Stats;
    }

    /** Attach fps{min,avg,max} + frametime_ms{avg,p95,max} + sample_count + engine avg to a payload. */
    void WriteStatsFields(const TSharedPtr<FJsonObject>& Data, const FCortexPerfStats& Stats, double DurationSeconds)
    {
        TSharedPtr<FJsonObject> Fps = MakeShared<FJsonObject>();
        Fps->SetNumberField(TEXT("min"), Stats.FpsMin);
        Fps->SetNumberField(TEXT("avg"), Stats.FpsAvg);
        Fps->SetNumberField(TEXT("max"), Stats.FpsMax);
        Data->SetObjectField(TEXT("fps"), Fps);

        TSharedPtr<FJsonObject> Frame = MakeShared<FJsonObject>();
        Frame->SetNumberField(TEXT("avg"), Stats.MsAvg);
        Frame->SetNumberField(TEXT("p95"), Stats.MsP95);
        Frame->SetNumberField(TEXT("max"), Stats.MsMax);
        Data->SetObjectField(TEXT("frametime_ms"), Frame);

        Data->SetNumberField(TEXT("sample_count"), Stats.Count);
        Data->SetNumberField(TEXT("duration"), DurationSeconds);
        Data->SetNumberField(TEXT("average_fps_engine"), GAverageFPS);
    }

    double ParseDuration(const TSharedPtr<FJsonObject>& Params)
    {
        double Duration = 3.0;
        if (Params.IsValid())
        {
            Params->TryGetNumberField(TEXT("duration"), Duration);
        }
        return FMath::Clamp(Duration, 0.5, 60.0);
    }

    /**
     * Run a per-frame sampling window on the active PIE world, then hand the collected
     * delta-seconds to BuildResult and fire DeferredCallback with its output. Returns a
     * synchronous PIE_NOT_ACTIVE error if PIE is not running, otherwise a deferred marker.
     */
    FCortexCommandResult StartPerfWindow(
        double DurationSeconds,
        TFunction<FCortexCommandResult(const TArray<double>&)> BuildResult,
        FDeferredResponseCallback DeferredCallback)
    {
        UWorld* PIEWorld = FCortexQAUtils::GetPIEWorld();
        if (PIEWorld == nullptr)
        {
            return FCortexQAUtils::PIENotActiveError();
        }

        TSharedPtr<TArray<double>> Samples = MakeShared<TArray<double>>();
        const double StartRealTime = FPlatformTime::Seconds();

        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
            [Samples, StartRealTime, DurationSeconds,
             BuildResult = MoveTemp(BuildResult),
             DeferredCallback = MoveTemp(DeferredCallback)](float DeltaTime) mutable -> bool
            {
                // PIE can end mid-window — bail like CortexQAActionOps::WaitFor does.
                if (GEditor == nullptr || GEditor->PlayWorld == nullptr)
                {
                    if (DeferredCallback)
                    {
                        DeferredCallback(FCortexCommandRouter::Error(
                            CortexErrorCodes::PIETerminated,
                            TEXT("PIE terminated during performance sampling window")));
                    }
                    return false; // unregister
                }

                if (DeltaTime > 0.0f)
                {
                    Samples->Add(static_cast<double>(DeltaTime));
                }

                if (FPlatformTime::Seconds() - StartRealTime >= DurationSeconds)
                {
                    if (DeferredCallback)
                    {
                        DeferredCallback(BuildResult(*Samples));
                    }
                    return false; // window complete, unregister
                }

                return true; // keep sampling
            }),
            0.0f); // fire every frame

        FCortexCommandResult Deferred;
        Deferred.bIsDeferred = true;
        return Deferred;
    }
}

FCortexCommandResult FCortexQAPerfOps::SamplePerformance(const TSharedPtr<FJsonObject>& Params, FDeferredResponseCallback DeferredCallback)
{
    const double DurationSeconds = ParseDuration(Params);

    auto Build = [DurationSeconds](const TArray<double>& Deltas) -> FCortexCommandResult
    {
        const FCortexPerfStats Stats = ComputeStats(Deltas);
        if (Stats.Count == 0)
        {
            return FCortexCommandRouter::Error(
                CortexErrorCodes::InvalidOperation,
                TEXT("No frames were sampled during the window"));
        }

        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        WriteStatsFields(Data, Stats, DurationSeconds);
        return FCortexCommandRouter::Success(Data);
    };

    return StartPerfWindow(DurationSeconds, MoveTemp(Build), MoveTemp(DeferredCallback));
}

FCortexCommandResult FCortexQAPerfOps::AssertFps(const TSharedPtr<FJsonObject>& Params, FDeferredResponseCallback DeferredCallback)
{
    double MinFps = 0.0;
    if (!Params.IsValid() || !Params->TryGetNumberField(TEXT("min_fps"), MinFps))
    {
        return FCortexCommandRouter::Error(
            CortexErrorCodes::InvalidField,
            TEXT("Missing required param: min_fps"));
    }

    const double DurationSeconds = ParseDuration(Params);
    FString Message;
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("message"), Message);
    }

    auto Build = [DurationSeconds, MinFps, Message](const TArray<double>& Deltas) -> FCortexCommandResult
    {
        const FCortexPerfStats Stats = ComputeStats(Deltas);
        if (Stats.Count == 0)
        {
            return FCortexCommandRouter::Error(
                CortexErrorCodes::InvalidOperation,
                TEXT("No frames were sampled during the window"));
        }

        const bool bPassed = Stats.FpsAvg >= MinFps;
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetBoolField(TEXT("passed"), bPassed);
        Data->SetNumberField(TEXT("expected"), MinFps);
        Data->SetNumberField(TEXT("actual_value"), Stats.FpsAvg);
        Data->SetStringField(TEXT("message"), Message.IsEmpty()
            ? FString::Printf(TEXT("avg fps %.1f %s min_fps %.1f"), Stats.FpsAvg, bPassed ? TEXT(">=") : TEXT("<"), MinFps)
            : Message);
        WriteStatsFields(Data, Stats, DurationSeconds);
        return FCortexCommandRouter::Success(Data);
    };

    return StartPerfWindow(DurationSeconds, MoveTemp(Build), MoveTemp(DeferredCallback));
}

FCortexCommandResult FCortexQAPerfOps::AssertFrametime(const TSharedPtr<FJsonObject>& Params, FDeferredResponseCallback DeferredCallback)
{
    double MaxMs = 0.0;
    if (!Params.IsValid() || !Params->TryGetNumberField(TEXT("max_ms"), MaxMs))
    {
        return FCortexCommandRouter::Error(
            CortexErrorCodes::InvalidField,
            TEXT("Missing required param: max_ms"));
    }

    const double DurationSeconds = ParseDuration(Params);
    FString Message;
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("message"), Message);
    }

    auto Build = [DurationSeconds, MaxMs, Message](const TArray<double>& Deltas) -> FCortexCommandResult
    {
        const FCortexPerfStats Stats = ComputeStats(Deltas);
        if (Stats.Count == 0)
        {
            return FCortexCommandRouter::Error(
                CortexErrorCodes::InvalidOperation,
                TEXT("No frames were sampled during the window"));
        }

        const bool bPassed = Stats.MsP95 <= MaxMs;
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetBoolField(TEXT("passed"), bPassed);
        Data->SetNumberField(TEXT("expected"), MaxMs);
        Data->SetNumberField(TEXT("actual_value"), Stats.MsP95);
        Data->SetStringField(TEXT("message"), Message.IsEmpty()
            ? FString::Printf(TEXT("p95 frametime %.2fms %s max_ms %.2fms"), Stats.MsP95, bPassed ? TEXT("<=") : TEXT(">"), MaxMs)
            : Message);
        WriteStatsFields(Data, Stats, DurationSeconds);
        return FCortexCommandRouter::Success(Data);
    };

    return StartPerfWindow(DurationSeconds, MoveTemp(Build), MoveTemp(DeferredCallback));
}
