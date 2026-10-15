// Engine-version compatibility shims. One header concentrates cross-version
// API drift so the rest of the plugin builds unchanged on UE 5.4 through 5.8.
// Precedent: CortexSTCompat.h (StateTree). Keep helpers FORCEINLINE and free of
// behavior differences beyond the API signature they absorb.
#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/UObjectHash.h"
#include <type_traits>
#include <utility>

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
#include "CortexLegacyWidgetIdentity.h"
#endif

#if !UE_VERSION_OLDER_THAN(5, 8, 0)
#include "Containers/SharedString.h"
#endif

#if (ENGINE_MAJOR_VERSION > 5 || ENGINE_MINOR_VERSION >= 6) && __has_include("Animation/IAnimationSequenceCompiler.h")
#include "Animation/IAnimationSequenceCompiler.h"
#define CORTEX_HAS_ANIM_SEQUENCE_COMPILER_INTERFACE 1
#else
#include "AssetCompilingManager.h"
#define CORTEX_HAS_ANIM_SEQUENCE_COMPILER_INTERFACE 0
#endif

class FStringTable;
class UFunction;

// --- FJsonObject::Values key type -------------------------------------------
// UE 5.8 changed the FJsonObject::Values map key from FString to
// UE::FSharedString. CortexJsonKey() produces a lookup key for Values.Find()
// from either representation; CortexJsonKeyToString() converts an iterated
// map key back to FString.
#if !UE_VERSION_OLDER_THAN(5, 8, 0)

FORCEINLINE UE::FSharedString CortexJsonKey(const FString& Key)
{
	return UE::FSharedString(Key);
}

FORCEINLINE const UE::FSharedString& CortexJsonKey(const UE::FSharedString& Key)
{
	return Key;
}

FORCEINLINE FString CortexJsonKeyToString(const UE::FSharedString& Key)
{
	return FString(Key.ToView());
}

#else

FORCEINLINE const FString& CortexJsonKey(const FString& Key)
{
	return Key;
}

FORCEINLINE FString CortexJsonKeyToString(const FString& Key)
{
	return Key;
}

#endif

// --- FStringTable::SetSourceString ------------------------------------------
// UE 5.8 added a required DevNotes parameter. Templated so it works for any
// engine's FStringTable without this header needing its definition.
template <typename TableType, typename KeyType>
FORCEINLINE void CortexSetSourceString(TableType& Table, const KeyType& Key, const FString& Value)
{
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
	Table.SetSourceString(Key, Value, TEXT(""));
#else
	Table.SetSourceString(Key, Value);
#endif
}

// --- Package iteration (the flags overload replaced the bool in 5.8) ---------
template <typename PackageType, typename OperationType>
FORCEINLINE void CortexForEachObjectWithPackage(
    const PackageType* Package, OperationType&& Operation, bool bIncludeNestedObjects = true)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 8
    ::ForEachObjectWithPackage(Package, Forward<OperationType>(Operation), bIncludeNestedObjects);
#else
    ::ForEachObjectWithPackage(Package, Forward<OperationType>(Operation),
        bIncludeNestedObjects ? EGetObjectsFlags::IncludeNestedObjects : EGetObjectsFlags::None);
#endif
}

template <typename T, typename = void>
struct TCortexHasSensitiveNameComparison : std::false_type {};
template <typename T>
struct TCortexHasSensitiveNameComparison<T,
    std::void_t<decltype(std::declval<const T&>().CompareSensitive(std::declval<const T&>()))>> : std::true_type {};

// Preserve the native fast path where available and display-name case on 5.4.
template <typename NameType>
FORCEINLINE int32 CortexCompareNamesSensitive(const NameType& Left, const NameType& Right)
{
    if constexpr (TCortexHasSensitiveNameComparison<NameType>::value)
    {
        return Left.CompareSensitive(Right);
    }
    else { return Left.ToString().Compare(Right.ToString(), ESearchCase::CaseSensitive); }
}

template <typename T, typename = void>
struct TCortexHasIsGathering : std::false_type {};
template <typename T>
struct TCortexHasIsGathering<T, std::void_t<decltype(std::declval<const T&>().IsGathering())>> : std::true_type {};

template <typename RegistryType>
FORCEINLINE bool CortexIsAssetRegistryGathering(const RegistryType& Registry)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    return Registry.IsLoadingAssets();
#else
    if constexpr (TCortexHasIsGathering<RegistryType>::value)
    {
        return Registry.IsGathering();
    }
    else { return Registry.IsLoadingAssets(); }
#endif
}

// The registration query is public on newer engines. The older map getter is
// protected; tests of this query must explicitly skip that assertion on 5.4.
#if ENGINE_MAJOR_VERSION > 5 || ENGINE_MINOR_VERSION >= 6
template <typename SubsystemType, typename ActionType>
FORCEINLINE bool CortexHasContinuousInputInjection(SubsystemType& Subsystem, const ActionType* Action)
{
    return Subsystem.HasContinuousInputInjectionForAction(Action);
}
#endif

template <typename PropertyType>
FORCEINLINE void CortexSetPropertyElementSize(PropertyType& Property, int32 Size)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    Property.ElementSize = Size;
#else
    Property.SetElementSize(Size);
#endif
}

template <typename TextKeyType>
FORCEINLINE FString CortexTextKeyToString(const TextKeyType& Key)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    return FString(Key.GetChars());
#else
    return Key.ToString();
#endif
}

template <typename EditorDataType, typename StateType>
FORCEINLINE auto& CortexAddStateTreeSubTree(EditorDataType& Data, FName Name, StateType Type)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    auto& State = Data.AddSubTree(Name);
    State.Type = Type;
    return State;
#else
    return Data.AddSubTree(Name, Type);
#endif
}

// SetEnum is not exported by BlueprintGraph in 5.4. Its public virtual
// ReloadEnum implementation forwards directly to SetEnum within that module.
template <typename NodeType>
FORCEINLINE void CortexSetSwitchEnum(NodeType& Node, UEnum* Enum)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    Node.ReloadEnum(Enum);
#else
    Node.SetEnum(Enum);
#endif
}

template <typename BindingType, typename TreeType, typename WidgetType, typename AnimationType>
FORCEINLINE UObject* CortexFindAnimationRuntimeObject(
    const BindingType& Binding, const TreeType& Tree, WidgetType& Widget, AnimationType* Animation)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    return Binding.FindRuntimeObject(Tree, Widget);
#else
    return Binding.FindRuntimeObject(Tree, Widget, Animation, nullptr);
#endif
}

// Hash only authoring fields that the current engine actually stores. UE 5.4
// integer keys are stepped, and byte channels have constant extrapolation.
template <typename ArchiveType, typename ChannelType>
FORCEINLINE void CortexSerializeIntegerChannelInterpolation(ArchiveType& Ar, const ChannelType& Channel)
{
#if ENGINE_MAJOR_VERSION > 5 || ENGINE_MINOR_VERSION >= 6
    bool bInterpLinear = Channel.bInterpolateLinearKeys;
    Ar << bInterpLinear;
#endif
}

template <typename ArchiveType, typename ChannelType>
FORCEINLINE void CortexSerializeByteChannelExtrapolation(ArchiveType& Ar, const ChannelType& Channel)
{
#if ENGINE_MAJOR_VERSION > 5 || ENGINE_MINOR_VERSION >= 6
    uint8 PreExtrap = (uint8)Channel.PreInfinityExtrap.GetValue();
    uint8 PostExtrap = (uint8)Channel.PostInfinityExtrap.GetValue();
    Ar << PreExtrap;
    Ar << PostExtrap;
#endif
}

template <typename T, typename = void>
struct TCortexHasDynamicAnimationBinding : std::false_type {};
template <typename T>
struct TCortexHasDynamicAnimationBinding<T, std::void_t<decltype(std::declval<const T&>().DynamicBinding.Function)>> : std::true_type {};

template <typename BindingType>
FORCEINLINE constexpr bool CortexSupportsDynamicAnimationBindings()
{
    return TCortexHasDynamicAnimationBinding<BindingType>::value;
}

// UMG has no dynamic animation binding records in 5.4. Such a record can never
// be present there; inspection reports null and retains native handling later.
template <typename BindingType>
FORCEINLINE auto CortexGetDynamicBindingFunction(const BindingType& Binding)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 5
    return static_cast<UFunction*>(nullptr);
#else
    if constexpr (CortexSupportsDynamicAnimationBindings<BindingType>())
    {
        return Binding.DynamicBinding.Function.Get();
    }
    else { return static_cast<UFunction*>(nullptr); }
#endif
}

template <typename BindingType>
FORCEINLINE void CortexSetDynamicBindingFunction(BindingType& Binding, UFunction* Function)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 5
    checkf(Function == nullptr, TEXT("Dynamic UMG animation bindings are unavailable before UE 5.5"));
#else
    if constexpr (CortexSupportsDynamicAnimationBindings<BindingType>())
    {
        Binding.DynamicBinding.Function = Function;
    }
    else { checkf(Function == nullptr, TEXT("Dynamic UMG animation bindings are unavailable on this engine")); }
#endif
}

// Native Designer GUID storage arrived after 5.4. Read-only lookups must not
// attach an extension during a refused preflight. For a compiled legacy asset,
// derive the same deterministic defaults in a weak-keyed cache until first write.
template <typename BlueprintType>
FORCEINLINE const auto& CortexReadWidgetVariableGuids(const BlueprintType& Blueprint)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    for (UBlueprintExtension* Extension : Blueprint.GetExtensions())
    {
        if (const auto* Identity = Cast<UCortexLegacyWidgetIdentity>(Extension))
        {
            return Identity->VariableGuids;
        }
    }
    static TMap<TWeakObjectPtr<const UObject>, TMap<FName, FGuid>> DefaultsByBlueprint;
    for (auto It = DefaultsByBlueprint.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid()) { It.RemoveCurrent(); }
    }
    TMap<FName, FGuid> Defaults;
    if (Blueprint.GeneratedClass)
    {
        Blueprint.ForEachSourceWidget([&Defaults](auto* Widget)
        {
            if (Widget) { Defaults.Add(Widget->GetFName(), FGuid::NewDeterministicGuid(Widget->GetPathName())); }
        });
        for (const auto& Animation : Blueprint.Animations)
        {
            if (Animation) { Defaults.Add(Animation->GetFName(), FGuid::NewDeterministicGuid(Animation->GetPathName())); }
        }
    }
    auto& Cached = DefaultsByBlueprint.FindOrAdd(TWeakObjectPtr<const UObject>(&Blueprint));
    if (!Cached.OrderIndependentCompareEqual(Defaults)) { Cached = MoveTemp(Defaults); }
    return static_cast<const TMap<FName, FGuid>&>(Cached);
#else
    return Blueprint.WidgetVariableNameToGuidMap;
#endif
}

// Writes use a serialized, transactional Blueprint extension on old engines.
// Native 5.4 compilers resolve widget references by name; Cortex preserves the
// authoring GUIDs across save/reload, duplication and undo via this extension.
template <typename BlueprintType>
FORCEINLINE auto& CortexWidgetVariableGuids(BlueprintType& Blueprint)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    UCortexLegacyWidgetIdentity* Identity = nullptr;
    for (UBlueprintExtension* Extension : Blueprint.GetExtensions())
    {
        Identity = Cast<UCortexLegacyWidgetIdentity>(Extension);
        if (Identity) { break; }
    }
    if (!Identity)
    {
        const TMap<FName, FGuid> Defaults = CortexReadWidgetVariableGuids(Blueprint);
        Blueprint.Modify(false);
        Identity = NewObject<UCortexLegacyWidgetIdentity>(&Blueprint, NAME_None, RF_Transactional);
        Blueprint.AddExtension(Identity);
        Identity->VariableGuids = Defaults;
    }
    Identity->Modify(false);
    return Identity->VariableGuids;
#else
    return Blueprint.WidgetVariableNameToGuidMap;
#endif
}

template <typename BlueprintType>
FORCEINLINE void CortexOnWidgetVariableAdded(BlueprintType& Blueprint, const FName& Name)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    CortexWidgetVariableGuids(Blueprint).Add(Name, FGuid::NewGuid());
#else
    Blueprint.OnVariableAdded(Name);
#endif
}

class UWidgetBlueprint;
class UWidget;

// Implemented in CortexUMG so CortexCore does not acquire UMG dependencies.
// The specializations use the native programmatic utility when available and
// the equivalent headless 5.4 rename sequence otherwise.
template <typename BlueprintType, typename WidgetType>
bool CortexVerifyWidgetRename(BlueprintType* Blueprint, WidgetType* Widget, const FText& Name, FText& Error);

template <typename BlueprintType, typename WidgetType>
bool CortexRenameWidget(BlueprintType* Blueprint, WidgetType* Widget, const FString& Name);

template <>
bool CortexVerifyWidgetRename(UWidgetBlueprint* Blueprint, UWidget* Widget, const FText& Name, FText& Error);

template <>
bool CortexRenameWidget(UWidgetBlueprint* Blueprint, UWidget* Widget, const FString& Name);

// Root parameter accessors replaced direct public storage in StateTree 5.6.
template <typename EditorDataType>
FORCEINLINE FGuid CortexGetStateTreeRootParametersGuid(const EditorDataType& Data)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    return Data.RootParameters.ID;
#else
    return Data.GetRootParametersGuid();
#endif
}

template <typename EditorDataType>
FORCEINLINE const auto& CortexGetStateTreeRootParameters(const EditorDataType& Data)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    return Data.RootParameters.Parameters;
#else
    return Data.GetRootParametersPropertyBag();
#endif
}

// Utility considerations are absent from pre-5.5 StateTree state storage.
// Callers can distinguish an absent engine capability from an empty native list.
template <typename StateType>
FORCEINLINE auto* CortexGetStateTreeConsiderations(StateType& State)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 5
    using ArrayType = std::remove_reference_t<decltype(State.EnterConditions)>;
    using QualifiedArrayType = std::conditional_t<std::is_const_v<StateType>, const ArrayType, ArrayType>;
    return static_cast<QualifiedArrayType*>(nullptr);
#else
    return &State.Considerations;
#endif
}

// Common path names changed when StateTree adopted PropertyBindingUtils in 5.6.
struct FStateTreePropertyPath;
struct FStateTreePropertyPathSegment;
struct FPropertyBindingPath;
struct FPropertyBindingPathSegment;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
using FCortexStateTreeBindingPath = FStateTreePropertyPath;
using FCortexStateTreeBindingPathSegment = FStateTreePropertyPathSegment;
#else
using FCortexStateTreeBindingPath = FPropertyBindingPath;
using FCortexStateTreeBindingPathSegment = FPropertyBindingPathSegment;
#endif

template <typename BindingsType, typename PathType>
FORCEINLINE void CortexRemoveStateTreeBindings(BindingsType& Bindings, const PathType& Path)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    Bindings.RemovePropertyBindings(Path);
#else
    Bindings.RemoveBindings(Path);
#endif
}

template <typename StateType>
FORCEINLINE const auto& CortexStateTreeConsiderations(const StateType& State)
{
    using ArrayType = std::remove_cv_t<std::remove_reference_t<decltype(State.EnterConditions)>>;
    static const ArrayType Empty;
    const auto* Considerations = CortexGetStateTreeConsiderations(State);
    return Considerations ? *Considerations : Empty;
}

FORCEINLINE constexpr bool CortexStateTreeHasConsiderations()
{
    return ENGINE_MAJOR_VERSION > 5 || ENGINE_MINOR_VERSION >= 5;
}

FORCEINLINE constexpr bool CortexStateTreeHasDescriptions()
{
    return ENGINE_MAJOR_VERSION > 5 || ENGINE_MINOR_VERSION >= 5;
}

template <typename StateType>
FORCEINLINE FString CortexGetStateTreeDescription(const StateType& State)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 5
    return FString();
#else
    return State.Description;
#endif
}

template <typename StateType>
FORCEINLINE bool CortexSetStateTreeDescription(StateType& State, const FString& Description)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 5
    return false;
#else
    State.Description = Description;
    return true;
#endif
}

// The animation-specific compiling manager was private in older engines.
// The public asset manager forwards to registered compilers for these objects.
template <typename SequenceType>
FORCEINLINE void CortexFinishAnimationCompilation(SequenceType& Sequence)
{
#if CORTEX_HAS_ANIM_SEQUENCE_COMPILER_INTERFACE
    auto* Animation = &Sequence;
    UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(MakeArrayView(&Animation, 1));
#else
    UObject* Asset = &Sequence;
    FAssetCompilingManager::Get().FinishCompilationForObjects(MakeArrayView(&Asset, 1));
#endif
}

template <typename StateType>
FORCEINLINE float CortexGetStateTreeWeight(const StateType& State)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 5
    return 1.0f; // Ordered selection has no stored utility weight on 5.4.
#else
    return State.Weight;
#endif
}

template <typename StateType>
FORCEINLINE bool CortexSetStateTreeWeight(StateType& State, float Weight)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 5
    return false;
#else
    State.Weight = Weight;
    return true;
#endif
}
