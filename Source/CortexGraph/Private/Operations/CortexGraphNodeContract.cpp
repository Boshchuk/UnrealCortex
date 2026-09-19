#include "Operations/CortexGraphNodeContract.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableSet.h"
#include "K2Node_VariableGet.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Self.h"
#include "K2Node_Knot.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Timeline.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Composite.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchString.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_CreateDelegate.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "Components/Widget.h"
#include "Blueprint/WidgetTree.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
const FString VarGetPrerequisite = TEXT("Referenced UMG designer widgets must have is_variable=true; call umg.set_widget_variable before referencing them from a graph.");
const FString VarGetNonRetryable = TEXT("VARIABLE_NOT_FOUND, INVALID_FIELD");
const FString CallFunctionNonRetryable = TEXT("INVALID_FIELD");

UClass* ResolveGraphNodeClassIdentifier(const FString& ClassIdentifier)
{
	if (ClassIdentifier.IsEmpty())
	{
		return nullptr;
	}

	if (UClass* FoundClass = FindObject<UClass>(nullptr, *ClassIdentifier))
	{
		return FoundClass;
	}

	if (!ClassIdentifier.StartsWith(TEXT("/")))
	{
		if (UClass* FoundClass = FindFirstObject<UClass>(*ClassIdentifier, EFindFirstObjectOptions::NativeFirst))
		{
			return FoundClass;
		}

		const FString EnginePath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassIdentifier);
		if (UClass* EngineClass = FindObject<UClass>(nullptr, *EnginePath))
		{
			return EngineClass;
		}
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		if (!IsValid(Candidate))
		{
			continue;
		}

		if (Candidate->GetName() == ClassIdentifier || Candidate->GetPathName() == ClassIdentifier)
		{
			return Candidate;
		}
	}

	if (!ClassIdentifier.StartsWith(TEXT("/")))
	{
		return nullptr;
	}

	// In-memory Blueprint asset (no disk access, avoids SkipPackage warnings).
	if (UBlueprint* InMemoryBlueprint = FindObject<UBlueprint>(nullptr, *ClassIdentifier))
	{
		return InMemoryBlueprint->GeneratedClass;
	}

	const FString PackageName = FPackageName::ObjectPathToPackageName(ClassIdentifier);
	const bool bPackageExists =
		PackageName.StartsWith(TEXT("/"))
		&& (FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName));
	if (!bPackageExists)
	{
		return nullptr;
	}

	if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *ClassIdentifier))
	{
		return LoadedClass;
	}

	if (UBlueprint* BlueprintAsset = LoadObject<UBlueprint>(nullptr, *ClassIdentifier))
	{
		return BlueprintAsset->GeneratedClass;
	}

	return nullptr;
}
}

TSharedRef<FJsonObject> FCortexNodeConstructionContract::ToJson() const
{
	TSharedRef<FJsonObject> Contract = MakeShared<FJsonObject>();
	Contract->SetStringField(TEXT("node_class"), NodeClass);
	Contract->SetStringField(TEXT("resolved_class"), ResolvedClass);
	Contract->SetBoolField(TEXT("supported"), bSupported);

	auto ParamToJson = [](const FCortexNodeConstructionParam& Param) -> TSharedRef<FJsonObject>
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Param.Name);
		Obj->SetStringField(TEXT("type"), Param.Type);
		Obj->SetBoolField(TEXT("required"), Param.bRequired);
		Obj->SetStringField(TEXT("description"), Param.Description);
		return Obj;
	};

	TArray<TSharedPtr<FJsonValue>> RequiredJson;
	for (const FCortexNodeConstructionParam& Param : RequiredParams)
	{
		RequiredJson.Add(MakeShared<FJsonValueObject>(ParamToJson(Param)));
	}
	Contract->SetArrayField(TEXT("required_params"), RequiredJson);

	TArray<TSharedPtr<FJsonValue>> OptionalJson;
	for (const FCortexNodeConstructionParam& Param : OptionalParams)
	{
		OptionalJson.Add(MakeShared<FJsonValueObject>(ParamToJson(Param)));
	}
	Contract->SetArrayField(TEXT("optional_params"), OptionalJson);

	TArray<TSharedPtr<FJsonValue>> SelectorJson;
	for (const FString& Selector : Selectors)
	{
		SelectorJson.Add(MakeShared<FJsonValueString>(Selector));
	}
	Contract->SetArrayField(TEXT("selectors"), SelectorJson);

	TArray<TSharedPtr<FJsonValue>> PinJson;
	for (const FCortexNodePinPreview& Pin : ExpectedPins)
	{
		TSharedRef<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin.Name);
		PinObj->SetStringField(TEXT("direction"), Pin.Direction);
		PinObj->SetStringField(TEXT("type"), Pin.Type);
		PinJson.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	Contract->SetArrayField(TEXT("expected_pins"), PinJson);
	Contract->SetBoolField(TEXT("pins_allocated"), bPinsAllocated);
	Contract->SetStringField(TEXT("prerequisites"), Prerequisites);
	Contract->SetStringField(TEXT("non_retryable_errors"), NonRetryableErrors);
	return Contract;
}

FCortexNodeConstructionContract FCortexGraphNodeContract::Describe(const FString& NodeClassName)
{
	FCortexNodeConstructionContract Contract;
	Contract.NodeClass = NodeClassName;

	auto Set = [&Contract](const FString& ResolvedClass) -> void
	{
		Contract.ResolvedClass = ResolvedClass;
		Contract.bSupported = true;
	};

	if (NodeClassName == TEXT("UK2Node_CallFunction"))
	{
		Set(TEXT("K2Node_CallFunction"));
		Contract.RequiredParams.Add({ TEXT("function_name"), TEXT("string"), true, TEXT("Canonical owner/function selector in ClassName.FunctionName format, e.g. KismetSystemLibrary.PrintString") });
		Contract.Prerequisites = TEXT("Resolve the function selector with describe_node before wiring pins.");
		Contract.NonRetryableErrors = CallFunctionNonRetryable;
	}
	else if (NodeClassName == TEXT("UK2Node_IfThenElse"))
	{
		Set(TEXT("K2Node_IfThenElse"));
	}
	else if (NodeClassName == TEXT("UK2Node_VariableSet") || NodeClassName == TEXT("UK2Node_VariableGet"))
	{
		Set(NodeClassName == TEXT("UK2Node_VariableSet") ? TEXT("K2Node_VariableSet") : TEXT("K2Node_VariableGet"));
		Contract.RequiredParams.Add({ TEXT("variable_name"), TEXT("string"), true, TEXT("Property or Blueprint variable name to reference") });
		Contract.OptionalParams.Add({ TEXT("variable_class"), TEXT("string"), false, TEXT("Owner class for external class properties, e.g. Actor for bHidden") });
		Contract.Prerequisites = VarGetPrerequisite;
		Contract.NonRetryableErrors = VarGetNonRetryable;
	}
	else if (NodeClassName == TEXT("UK2Node_Event") || NodeClassName == TEXT("Event"))
	{
		Set(TEXT("K2Node_Event"));
		Contract.OptionalParams.Add({ TEXT("function_name"), TEXT("string"), false, TEXT("Event selector in ClassName.EventName format, e.g. Actor.ReceiveBeginPlay") });
		Contract.NonRetryableErrors = TEXT("INVALID_FIELD");
	}
	else if (NodeClassName == TEXT("UK2Node_ExecutionSequence"))
	{
		Set(TEXT("K2Node_ExecutionSequence"));
	}
	else if (NodeClassName == TEXT("UK2Node_CustomEvent"))
	{
		Set(TEXT("K2Node_CustomEvent"));
	}
	else if (NodeClassName == TEXT("UK2Node_Self"))
	{
		Set(TEXT("K2Node_Self"));
	}
	else if (NodeClassName == TEXT("UK2Node_Knot"))
	{
		Set(TEXT("K2Node_Knot"));
	}
	else if (NodeClassName == TEXT("UK2Node_MakeArray"))
	{
		Set(TEXT("K2Node_MakeArray"));
	}
	else if (NodeClassName == TEXT("UK2Node_Timeline"))
	{
		Set(TEXT("K2Node_Timeline"));
		Contract.RequiredParams.Add({ TEXT("timeline_name"), TEXT("string"), true, TEXT("Name of the UTimelineTemplate already present on the Blueprint") });
		Contract.NonRetryableErrors = TEXT("INVALID_FIELD, VARIABLE_NOT_FOUND");
	}
	else if (NodeClassName == TEXT("UK2Node_SpawnActorFromClass"))
	{
		Set(TEXT("K2Node_SpawnActorFromClass"));
	}
	else if (NodeClassName == TEXT("UK2Node_DynamicCast"))
	{
		Set(TEXT("K2Node_DynamicCast"));
		Contract.OptionalParams.Add({ TEXT("class"), TEXT("string"), false, TEXT("Cast target class; alias target_class accepted") });
		Contract.NonRetryableErrors = TEXT("INVALID_FIELD, CLASS_NOT_FOUND");
	}
	else if (NodeClassName == TEXT("UK2Node_MacroInstance"))
	{
		Set(TEXT("K2Node_MacroInstance"));
		Contract.RequiredParams.Add({ TEXT("macro_path"), TEXT("string"), true, TEXT("Asset path to the macro Blueprint graph") });
		Contract.NonRetryableErrors = TEXT("INVALID_FIELD, ASSET_NOT_FOUND");
	}
	else if (NodeClassName == TEXT("UK2Node_SwitchEnum"))
	{
		Set(TEXT("K2Node_SwitchEnum"));
		Contract.OptionalParams.Add({ TEXT("enum_name"), TEXT("string"), false, TEXT("Reflected enum to switch on; pins allocate from its entries") });
		Contract.NonRetryableErrors = TEXT("INVALID_FIELD, CLASS_NOT_FOUND");
	}
	else if (NodeClassName == TEXT("UK2Node_SwitchString"))
	{
		Set(TEXT("K2Node_SwitchString"));
	}
	else if (NodeClassName == TEXT("UK2Node_SwitchInteger"))
	{
		Set(TEXT("K2Node_SwitchInteger"));
	}
	else if (NodeClassName == TEXT("UK2Node_AddDelegate") || NodeClassName == TEXT("UK2Node_RemoveDelegate") || NodeClassName == TEXT("UK2Node_ClearDelegate"))
	{
		Set(NodeClassName == TEXT("UK2Node_AddDelegate") ? TEXT("K2Node_AddDelegate")
			: NodeClassName == TEXT("UK2Node_RemoveDelegate") ? TEXT("K2Node_RemoveDelegate") : TEXT("K2Node_ClearDelegate"));
		Contract.RequiredParams.Add({ TEXT("delegate_name"), TEXT("string"), true, TEXT("Multicast delegate name") });
		Contract.OptionalParams.Add({ TEXT("delegate_class"), TEXT("string"), false, TEXT("Owner class for external delegates; omit for self-context event dispatchers") });
		Contract.NonRetryableErrors = TEXT("INVALID_FIELD, VARIABLE_NOT_FOUND");
	}
	else if (NodeClassName == TEXT("UK2Node_CreateDelegate"))
	{
		Set(TEXT("K2Node_CreateDelegate"));
		Contract.OptionalParams.Add({ TEXT("function_name"), TEXT("string"), false, TEXT("Target function name (bare name)") });
		Contract.NonRetryableErrors = TEXT("INVALID_FIELD");
	}
	else if (NodeClassName == TEXT("UK2Node_Composite") || NodeClassName == TEXT("Composite"))
	{
		Set(TEXT("K2Node_Composite"));
	}
	else
	{
		Contract.ResolvedClass = TEXT("");
		Contract.bSupported = false;
	}
	return Contract;
}

bool FCortexGraphNodeContract::Validate(
	const FString& NodeClassName,
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& NodeParams,
	FCortexCommandResult& OutError)
{
	const FCortexNodeConstructionContract Contract = Describe(NodeClassName);
	if (!Contract.bSupported)
	{
		OutError = FCortexCommandRouter::Error(
			CortexErrorCodes::InvalidField,
			FString::Printf(TEXT("Node class not supported: %s"), *NodeClassName));
		return false;
	}

	auto Fail = [&OutError, &Contract](const FString& Field, const FString& Message) -> bool
	{
		TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
		Details->SetStringField(TEXT("field"), Field);
		Details->SetStringField(TEXT("message"), Message);
		Details->SetObjectField(TEXT("describe_node"), Contract.ToJson());
		OutError = FCortexCommandRouter::Error(CortexErrorCodes::InvalidField, Message, Details);
		return false;
	};

	if (NodeClassName == TEXT("UK2Node_CallFunction"))
	{
		FString FunctionName;
		if (!NodeParams.IsValid() || !NodeParams->TryGetStringField(TEXT("function_name"), FunctionName) || FunctionName.IsEmpty())
		{
			return Fail(TEXT("params.function_name"), TEXT("CallFunction requires params.function_name in ClassName.FunctionName format"));
		}
		FString ClassName;
		FString FuncName;
		if (!FunctionName.Split(TEXT("."), &ClassName, &FuncName) || ClassName.IsEmpty() || FuncName.IsEmpty())
		{
			return Fail(TEXT("params.function_name"), FString::Printf(TEXT("Malformed function selector '%s'; expected ClassName.FunctionName"), *FunctionName));
		}
		UClass* FuncClass = FindFirstObject<UClass>(*ClassName);
		if (FuncClass == nullptr)
		{
			return Fail(TEXT("params.function_name"), FString::Printf(TEXT("Function owner class not found: %s"), *ClassName));
		}
		if (FuncClass->FindFunctionByName(FName(*FuncName)) == nullptr)
		{
			return Fail(TEXT("params.function_name"), FString::Printf(TEXT("Function not found: %s on class %s"), *FuncName, *ClassName));
		}
	}
	else if (NodeClassName == TEXT("UK2Node_VariableSet") || NodeClassName == TEXT("UK2Node_VariableGet"))
	{
		FString VariableName;
		if (!NodeParams.IsValid() || !NodeParams->TryGetStringField(TEXT("variable_name"), VariableName) || VariableName.IsEmpty())
		{
			return Fail(TEXT("params.variable_name"), FString::Printf(TEXT("%s requires params.variable_name"), *NodeClassName));
		}
		FString VariableClass;
		NodeParams->TryGetStringField(TEXT("variable_class"), VariableClass);
		if (!VariableClass.IsEmpty())
		{
			UClass* VarClass = FindFirstObject<UClass>(*VariableClass);
			if (VarClass == nullptr)
			{
				return Fail(TEXT("params.variable_class"), FString::Printf(TEXT("Variable owner class not found: %s"), *VariableClass));
			}
			if (VarClass->FindPropertyByName(FName(*VariableName)) == nullptr)
			{
				return Fail(TEXT("params.variable_name"), FString::Printf(TEXT("Property not found: %s on class %s"), *VariableName, *VariableClass));
			}
		}
		else
			{
				UClass* SelfClass = Blueprint->SkeletonGeneratedClass
					? Blueprint->SkeletonGeneratedClass
					: Blueprint->GeneratedClass;
				FProperty* Member = nullptr;
				if (SelfClass)
				{
					Member = SelfClass->FindPropertyByName(FName(*VariableName));
				}
				// Designer widgets are Widget Tree members, not reflected FProperties or
				// FBPVariableDescription entries. Resolve them BEFORE the generic self-property
				// failure so a non-variable designer widget gets the actionable
				// umg.set_widget_variable guidance, and a referenceable one validates cleanly.
				if (UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Blueprint))
				{
					if (UWidget* Widget = WBP->WidgetTree ? WBP->WidgetTree->FindWidget(FName(*VariableName)) : nullptr)
					{
						if (!Widget->bIsVariable)
						{
							return Fail(TEXT("params.variable_name"),
								FString::Printf(TEXT("Designer widget '%s' has is_variable=false and cannot be referenced from a graph; call umg.set_widget_variable first."), *VariableName));
						}
						return true;
					}
				}
				// Blueprint member variables are FBPVariableDescription entries (NewVariables), not
				// reflected FProperties; FindNewVariableIndex returns INDEX_NONE when absent.
				if (Member == nullptr
					&& FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(*VariableName)) == INDEX_NONE)
				{
					return Fail(TEXT("params.variable_name"), FString::Printf(TEXT("Self property not found: %s"), *VariableName));
				}
			}
	}
	else if (NodeClassName == TEXT("UK2Node_Timeline"))
	{
		FString TimelineName;
		if (!NodeParams.IsValid() || !NodeParams->TryGetStringField(TEXT("timeline_name"), TimelineName) || TimelineName.IsEmpty())
		{
			return Fail(TEXT("params.timeline_name"), TEXT("Timeline requires params.timeline_name"));
		}
	}
	else if (NodeClassName == TEXT("UK2Node_MacroInstance"))
	{
		FString MacroPath;
		if (!NodeParams.IsValid() || !NodeParams->TryGetStringField(TEXT("macro_path"), MacroPath) || MacroPath.IsEmpty())
		{
			return Fail(TEXT("params.macro_path"), TEXT("MacroInstance requires params.macro_path"));
		}
	}
	else if (NodeClassName == TEXT("UK2Node_AddDelegate") || NodeClassName == TEXT("UK2Node_RemoveDelegate") || NodeClassName == TEXT("UK2Node_ClearDelegate"))
	{
		FString DelegateName;
		if (!NodeParams.IsValid() || !NodeParams->TryGetStringField(TEXT("delegate_name"), DelegateName) || DelegateName.IsEmpty())
		{
			return Fail(TEXT("params.delegate_name"), FString::Printf(TEXT("%s requires params.delegate_name"), *NodeClassName));
		}
		FString DelegateClass;
		NodeParams->TryGetStringField(TEXT("delegate_class"), DelegateClass);
		if (!DelegateClass.IsEmpty())
		{
			UClass* OwnerClass = FindFirstObject<UClass>(*DelegateClass);
			if (OwnerClass == nullptr || CastField<FMulticastDelegateProperty>(OwnerClass->FindPropertyByName(FName(*DelegateName))) == nullptr)
			{
				return Fail(TEXT("params.delegate_name"), FString::Printf(TEXT("Multicast delegate property not found: %s on class %s"), *DelegateName, *DelegateClass));
			}
		}
		else
		{
			UClass* SelfClass = Blueprint->SkeletonGeneratedClass
				? Blueprint->SkeletonGeneratedClass
				: Blueprint->GeneratedClass;
			if (SelfClass == nullptr || CastField<FMulticastDelegateProperty>(SelfClass->FindPropertyByName(FName(*DelegateName))) == nullptr)
			{
				return Fail(TEXT("params.delegate_name"), FString::Printf(TEXT("Self delegate property not found: %s"), *DelegateName));
			}
		}
	}
	else if (NodeClassName == TEXT("UK2Node_DynamicCast"))
	{
		FString TargetClassIdentifier;
		const bool bHasClass = NodeParams.IsValid() && (
			NodeParams->TryGetStringField(TEXT("class"), TargetClassIdentifier)
			|| NodeParams->TryGetStringField(TEXT("target_class"), TargetClassIdentifier));
		if (bHasClass && ResolveGraphNodeClassIdentifier(TargetClassIdentifier) == nullptr)
		{
			return Fail(TEXT("params.class"), FString::Printf(TEXT("Cast target class not found: %s"), *TargetClassIdentifier));
		}
	}
	else if (NodeClassName == TEXT("UK2Node_SwitchEnum"))
	{
		FString EnumName;
		if (NodeParams.IsValid() && NodeParams->TryGetStringField(TEXT("enum_name"), EnumName) && !EnumName.IsEmpty())
		{
			if (FindFirstObject<UEnum>(*EnumName) == nullptr)
			{
				return Fail(TEXT("params.enum_name"), FString::Printf(TEXT("Enum not found: %s"), *EnumName));
			}
		}
	}
	else if (NodeClassName == TEXT("UK2Node_Event") || NodeClassName == TEXT("Event"))
	{
		FString FunctionName;
		if (NodeParams.IsValid() && NodeParams->TryGetStringField(TEXT("function_name"), FunctionName) && !FunctionName.IsEmpty())
		{
			FString ClassName;
			FString FuncName;
			UClass* EventClass = nullptr;
			if (!FunctionName.Split(TEXT("."), &ClassName, &FuncName) || ClassName.IsEmpty())
			{
				return Fail(TEXT("params.function_name"), FString::Printf(TEXT("Malformed event selector '%s'"), *FunctionName));
			}
			EventClass = FindFirstObject<UClass>(*ClassName);
			if (EventClass == nullptr || EventClass->FindFunctionByName(FName(*FuncName)) == nullptr)
			{
				return Fail(TEXT("params.function_name"), FString::Printf(TEXT("Event function not found: %s"), *FunctionName));
			}
		}
	}

	return true;
}

bool FCortexGraphNodeContract::ApplyNodeConstructionParams(
	UEdGraph* Graph,
	UEdGraphNode* NewNode,
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& NodeParams,
	FString& OutError)
{
	(void)Graph;
	if (!NodeParams.IsValid())
	{
		return true;
	}

	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(NewNode))
	{
		FString FunctionName;
		if (NodeParams->TryGetStringField(TEXT("function_name"), FunctionName))
		{
			FString ClassName;
			FString FuncName;
			if (FunctionName.Split(TEXT("."), &ClassName, &FuncName))
			{
				UClass* FuncClass = FindFirstObject<UClass>(*ClassName);
				UFunction* Func = FuncClass ? FuncClass->FindFunctionByName(FName(*FuncName)) : nullptr;
				if (Func == nullptr)
				{
					OutError = FString::Printf(TEXT("Function not found: %s on class %s"), *FuncName, *ClassName);
					return false;
				}
				CallNode->SetFromFunction(Func);
			}
		}
	}

	if (UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(NewNode))
	{
		FString VariableName;
		if (NodeParams->TryGetStringField(TEXT("variable_name"), VariableName))
		{
			FString VariableClass;
			if (NodeParams->TryGetStringField(TEXT("variable_class"), VariableClass))
			{
				UClass* VarClass = FindFirstObject<UClass>(*VariableClass);
				FProperty* Prop = VarClass ? VarClass->FindPropertyByName(FName(*VariableName)) : nullptr;
				if (Prop == nullptr)
				{
					OutError = FString::Printf(TEXT("Property not found: %s on class %s"), *VariableName, *VariableClass);
					return false;
				}
				VarNode->SetFromProperty(Prop, false, VarClass);
			}
			else
			{
				VarNode->VariableReference.SetSelfMember(FName(*VariableName));
			}
		}
	}

	if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(NewNode))
	{
		FString TargetClassIdentifier;
		const bool bHasClass =
			NodeParams->TryGetStringField(TEXT("class"), TargetClassIdentifier)
			|| NodeParams->TryGetStringField(TEXT("target_class"), TargetClassIdentifier);
		if (bHasClass)
		{
			UClass* TargetClass = ResolveGraphNodeClassIdentifier(TargetClassIdentifier);
			if (TargetClass == nullptr)
			{
				OutError = FString::Printf(TEXT("Cast target class not found: %s"), *TargetClassIdentifier);
				return false;
			}
			CastNode->TargetType = TargetClass;
			CastNode->ReconstructNode();
		}
	}

	if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(NewNode))
	{
		FString FunctionName;
		if (NodeParams->TryGetStringField(TEXT("function_name"), FunctionName))
		{
			FString ClassName;
			FString FuncName;
			if (FunctionName.Split(TEXT("."), &ClassName, &FuncName))
			{
				UClass* FuncClass = FindFirstObject<UClass>(*ClassName);
				EventNode->EventReference.SetExternalMember(FName(*FuncName), FuncClass);
				EventNode->bOverrideFunction = true;
			}
		}
	}

	if (UK2Node_BaseMCDelegate* DelegateNode = Cast<UK2Node_BaseMCDelegate>(NewNode))
	{
		FString DelegateName;
		if (NodeParams->TryGetStringField(TEXT("delegate_name"), DelegateName))
		{
			FString DelegateClass;
			if (NodeParams->TryGetStringField(TEXT("delegate_class"), DelegateClass))
			{
				UClass* OwnerClass = FindFirstObject<UClass>(*DelegateClass);
				FMulticastDelegateProperty* DelegateProp = OwnerClass
					? CastField<FMulticastDelegateProperty>(OwnerClass->FindPropertyByName(FName(*DelegateName)))
					: nullptr;
				if (DelegateProp == nullptr)
				{
					OutError = FString::Printf(TEXT("Multicast delegate property not found: %s on class %s"), *DelegateName, *DelegateClass);
					return false;
				}
				DelegateNode->SetFromProperty(DelegateProp, false, OwnerClass);
			}
			else
			{
				UClass* SelfClass = Blueprint->SkeletonGeneratedClass
					? Blueprint->SkeletonGeneratedClass
					: Blueprint->GeneratedClass;
				FMulticastDelegateProperty* DelegateProp = SelfClass
					? CastField<FMulticastDelegateProperty>(SelfClass->FindPropertyByName(FName(*DelegateName)))
					: nullptr;
				if (DelegateProp == nullptr)
				{
					OutError = FString::Printf(TEXT("Self delegate property not found: %s"), *DelegateName);
					return false;
				}
				DelegateNode->SetFromProperty(DelegateProp, true, SelfClass);
			}
		}
	}

	if (UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(NewNode))
	{
		FString FunctionName;
		if (NodeParams->TryGetStringField(TEXT("function_name"), FunctionName))
		{
			CreateDelegateNode->SetFunction(FName(*FunctionName));
		}
	}

	if (UK2Node_SwitchEnum* SwitchEnumNode = Cast<UK2Node_SwitchEnum>(NewNode))
	{
		FString EnumName;
		if (NodeParams->TryGetStringField(TEXT("enum_name"), EnumName) && !EnumName.IsEmpty())
		{
			UEnum* Enum = FindFirstObject<UEnum>(*EnumName);
			if (Enum == nullptr)
			{
				OutError = FString::Printf(TEXT("Enum not found: %s"), *EnumName);
				return false;
			}
			SwitchEnumNode->SetEnum(Enum);
		}
	}

	return true;
}
