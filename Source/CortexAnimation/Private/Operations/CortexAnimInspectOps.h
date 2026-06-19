#pragma once

#include "CoreMinimal.h"
#include "CortexCommandRouter.h"

/** Read-only inspection of animation assets (Phase A of the CortexAnimation domain). */
class FCortexAnimInspectOps
{
public:
	static FCortexCommandResult ListAssets(const TSharedPtr<FJsonObject>& Params);
	static FCortexCommandResult GetSequenceInfo(const TSharedPtr<FJsonObject>& Params);
	static FCortexCommandResult GetMontageInfo(const TSharedPtr<FJsonObject>& Params);
	static FCortexCommandResult GetSkeletonInfo(const TSharedPtr<FJsonObject>& Params);
	static FCortexCommandResult GetAnimBlueprintInfo(const TSharedPtr<FJsonObject>& Params);
};
