#pragma once

#include "CoreMinimal.h"
#include "CortexTypes.h"

class FCortexEditorPIEState;

class FCortexEditorInputOps
{
public:
	static FCortexCommandResult InjectKey(
		TSharedPtr<FCortexEditorPIEState> PIEState,
		const TSharedPtr<FJsonObject>& Params);
	static FCortexCommandResult InjectMouse(
		const FCortexEditorPIEState& PIEState,
		const TSharedPtr<FJsonObject>& Params);
	static FCortexCommandResult InjectInputAction(
		const FCortexEditorPIEState& PIEState,
		const TSharedPtr<FJsonObject>& Params);
	static FCortexCommandResult InjectInputContinuous(
		TSharedPtr<FCortexEditorPIEState> PIEState,
		const TSharedPtr<FJsonObject>& Params,
		FDeferredResponseCallback DeferredCallback);
	static FCortexCommandResult InjectInputSequence(
		TSharedPtr<FCortexEditorPIEState> PIEState,
		const TSharedPtr<FJsonObject>& Params,
		FDeferredResponseCallback DeferredCallback);
};
