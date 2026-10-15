#pragma once

#include "CoreMinimal.h"
#include "Blueprint/BlueprintExtension.h"
#include "Templates/Function.h"
#include "CortexLegacyWidgetIdentity.generated.h"

class UBlueprint;

// UE 5.4/5.5 have no serialized Designer variable GUID map. Keep the same
// authoring identities in a transactional Blueprint extension on those engines.
UCLASS()
class CORTEXCORE_API UCortexLegacyWidgetIdentity : public UBlueprintExtension
{
    GENERATED_BODY()

public:
    UPROPERTY()
    TMap<FName, FGuid> VariableGuids;

    /**
     * Seeds an empty map from the owning Designer the way the UE 5.6+ widget compiler seeds its
     * empty native map (FWidgetBlueprintCompilerContext::ValidateAndFixUpVariableGuids): every
     * live source widget and animation receives its path-derived deterministic GUID. CortexUMG,
     * which owns the widget types, binds it on engines that use this extension.
     */
    static TFunction<void(UBlueprint& OwningBlueprint, TMap<FName, FGuid>& Guids)> SeedEmptyMapOnCompile;

#if WITH_EDITORONLY_DATA
private:
    // Runs once per compile of the owning Blueprint, like the native compiler's own seeding.
    virtual void HandleGenerateFunctionGraphs(FKismetCompilerContext* CompilerContext) override;
#endif
};
