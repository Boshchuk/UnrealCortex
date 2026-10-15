#pragma once

#include "CoreMinimal.h"
#include "Blueprint/BlueprintExtension.h"
#include "CortexLegacyWidgetIdentity.generated.h"

// UE 5.4/5.5 have no serialized Designer variable GUID map. Keep the same
// authoring identities in a transactional Blueprint extension on those engines.
UCLASS()
class CORTEXCORE_API UCortexLegacyWidgetIdentity : public UBlueprintExtension
{
    GENERATED_BODY()

public:
    UPROPERTY()
    TMap<FName, FGuid> VariableGuids;
};
