#include "CortexLegacyWidgetIdentity.h"

#include "Engine/Blueprint.h"
#include "Misc/EngineVersionComparison.h"

TFunction<void(UBlueprint& OwningBlueprint, TMap<FName, FGuid>& Guids)> UCortexLegacyWidgetIdentity::SeedEmptyMapOnCompile;

#if WITH_EDITORONLY_DATA
void UCortexLegacyWidgetIdentity::HandleGenerateFunctionGraphs(FKismetCompilerContext* CompilerContext)
{
    (void)CompilerContext;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    // The native 5.6+ compiler seeds only a map that is still empty; a populated map is the
    // authority and stays untouched (an undo that restores an empty map is re-seeded here).
    if (!VariableGuids.IsEmpty() || !SeedEmptyMapOnCompile)
    {
        return;
    }
    if (UBlueprint* OwningBlueprint = GetTypedOuter<UBlueprint>())
    {
        SeedEmptyMapOnCompile(*OwningBlueprint, VariableGuids);
    }
#endif
}
#endif
