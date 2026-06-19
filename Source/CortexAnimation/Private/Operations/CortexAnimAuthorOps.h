#pragma once

#include "CoreMinimal.h"
#include "CortexCommandRouter.h"

/** Mutating authoring of animation assets (Phase B of the CortexAnimation domain). */
class FCortexAnimAuthorOps
{
public:
	// AnimSequence notifies (direct Notifies array; no IAnimationDataController path in 5.4).
	static FCortexCommandResult AddNotify(const TSharedPtr<FJsonObject>& Params);
	static FCortexCommandResult RemoveNotify(const TSharedPtr<FJsonObject>& Params);

	// AnimSequence float curves (via IAnimationDataController).
	static FCortexCommandResult AddCurve(const TSharedPtr<FJsonObject>& Params);
	static FCortexCommandResult SetCurveKeys(const TSharedPtr<FJsonObject>& Params);
	static FCortexCommandResult RemoveCurve(const TSharedPtr<FJsonObject>& Params);

	// AnimMontage sections.
	static FCortexCommandResult AddMontageSection(const TSharedPtr<FJsonObject>& Params);
	static FCortexCommandResult RemoveMontageSection(const TSharedPtr<FJsonObject>& Params);

	// Skeleton sockets (direct Sockets array; no AddSocket method in 5.4).
	static FCortexCommandResult AddSocket(const TSharedPtr<FJsonObject>& Params);
	static FCortexCommandResult SetSocketTransform(const TSharedPtr<FJsonObject>& Params);
	static FCortexCommandResult RemoveSocket(const TSharedPtr<FJsonObject>& Params);
};
