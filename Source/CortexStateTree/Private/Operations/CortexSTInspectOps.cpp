#include "Operations/CortexSTInspectOps.h"

#include "CortexCommandRouter.h"
#include "CortexSTTypes.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTree.h"
#include "CortexSerializer.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyOptional.h"

namespace
{
// Supplement the pinned shared reader's missing small-integer/optional coverage
// locally; do not change serialization contracts for other provider domains.
void CompleteStoredValue(const FProperty* Property, const void* Memory, const FString& Path,
	TSharedPtr<FJsonValue>& Json, TArray<FCortexSerializationIssue>& Issues, int32 Depth = 0)
{
	if (Depth > 32)
	{
		return;
	}
	bool bCompleted = false;
	if (const FInt8Property* Number = CastField<FInt8Property>(Property))
	{
		Json = MakeShared<FJsonValueNumber>(Number->GetPropertyValue(Memory));
		bCompleted = true;
	}
	else if (const FUInt16Property* UnsignedNumber = CastField<FUInt16Property>(Property))
	{
		Json = MakeShared<FJsonValueNumber>(UnsignedNumber->GetPropertyValue(Memory));
		bCompleted = true;
	}
	else if (const FOptionalProperty* Optional = CastField<FOptionalProperty>(Property))
	{
		TSharedPtr<FJsonObject> Value = MakeShared<FJsonObject>();
		const void* Stored = Optional->GetValuePointerForReadIfSet(Memory);
		Value->SetBoolField(TEXT("is_set"), Stored != nullptr);
		if (Stored)
		{
			FCortexSerializationPolicy Policy;
			Policy.MaxDepth = 32 - Depth;
			FCortexPropertySerializationResult Inner = FCortexSerializer::PropertyToJsonDeep(Optional->GetValueProperty(), Stored, Policy, Path + TEXT(".value"));
			CompleteStoredValue(Optional->GetValueProperty(), Stored, Path + TEXT(".value"), Inner.JsonValue, Inner.Issues, Depth + 1);
			Value->SetField(TEXT("value"), Inner.JsonValue);
			Issues.Append(Inner.Issues);
		}
		Json = MakeShared<FJsonValueObject>(Value);
		bCompleted = true;
	}
	else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
	{
		const UStruct* Type = Struct->Struct;
		if (Struct->Struct == FInstancedStruct::StaticStruct())
		{
			const FInstancedStruct* Instance = static_cast<const FInstancedStruct*>(Memory);
			Type = Instance->GetScriptStruct();
			Memory = Instance->GetMemory();
		}
		if (Type && Memory && Json.IsValid() && Json->Type == EJson::Object)
		{
			for (TFieldIterator<FProperty> It(Type); It; ++It)
			{
				const FString FieldName = (*It)->GetName();
				TSharedPtr<FJsonValue> Child = Json->AsObject()->TryGetField(FieldName);
				if (Child.IsValid())
				{
					CompleteStoredValue(*It, (*It)->ContainerPtrToValuePtr<void>(Memory), Path + TEXT(".") + FieldName, Child, Issues, Depth + 1);
					Json->AsObject()->SetField(FieldName, Child);
				}
			}
		}
	}
	else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
	{
		if (Json.IsValid() && Json->Type == EJson::Array)
		{
			FScriptArrayHelper Helper(Array, Memory);
			TArray<TSharedPtr<FJsonValue>> Values = Json->AsArray();
			for (int32 Index = 0; Index < Helper.Num() && Index < Values.Num(); ++Index)
			{
				CompleteStoredValue(Array->Inner, Helper.GetRawPtr(Index), FString::Printf(TEXT("%s[%d]"), *Path, Index), Values[Index], Issues, Depth + 1);
			}
			Json = MakeShared<FJsonValueArray>(Values);
		}
	}
	if (bCompleted)
	{
		Issues.RemoveAll([&Path](const FCortexSerializationIssue& Issue) { return Issue.Field == Path && Issue.Code == TEXT("UNSUPPORTED_PROPERTY_TYPE"); });
	}
}

TSharedPtr<FJsonObject> InspectFields(const UStruct* Type, const void* Memory, const TSet<FName>& Excluded = {})
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (Type == nullptr || Memory == nullptr)
	{
		Result->SetBoolField(TEXT("available"), false);
		return Result;
	}
	Result->SetBoolField(TEXT("available"), true);
	Result->SetStringField(TEXT("type_path"), Type->GetPathName());
	TArray<TSharedPtr<FJsonValue>> Fields;
	for (TFieldIterator<FProperty> It(Type); It; ++It)
	{
		const FProperty* Property = *It;
		if (Excluded.Contains(Property->GetFName()))
		{
			continue;
		}
		TSharedPtr<FJsonObject> Field = MakeShared<FJsonObject>();
		Field->SetStringField(TEXT("name"), Property->GetName());
		Field->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
		Field->SetStringField(TEXT("origin"), Property->GetOwnerStruct()->GetPathName());
		Field->SetStringField(TEXT("property_flags"), FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(Property->GetPropertyFlags())));
		FCortexSerializationPolicy Policy;
		Policy.MaxDepth = 32;
		Policy.bExpandInstancedSubobjects = false;
		TArray<TSharedPtr<FJsonValue>> Values;
		TArray<FCortexSerializationIssue> Issues;
		bool bPartial = false;
		for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
		{
			FCortexPropertySerializationResult Value = FCortexSerializer::PropertyToJsonDeep(
				Property, Property->ContainerPtrToValuePtr<void>(Memory, Index), Policy, Property->GetName());
			CompleteStoredValue(Property, Property->ContainerPtrToValuePtr<void>(Memory, Index), Property->GetName(), Value.JsonValue, Value.Issues);
			Values.Add(Value.JsonValue.IsValid() ? Value.JsonValue : MakeShared<FJsonValueNull>());
			Issues.Append(Value.Issues);
			bPartial |= !Value.Issues.IsEmpty();
		}
		Field->SetField(TEXT("value"), Property->ArrayDim == 1 ? Values[0] : MakeShared<FJsonValueArray>(Values));
		Field->SetBoolField(TEXT("partial"), bPartial);
		Field->SetArrayField(TEXT("issues"), FCortexSerializer::SerializationIssuesToJson(Issues));
		Fields.Add(MakeShared<FJsonValueObject>(Field));
	}
	Result->SetArrayField(TEXT("fields"), Fields);
	return Result;
}

TSharedPtr<FJsonObject> InspectBag(const FInstancedPropertyBag& Bag)
{
	const FConstStructView View = Bag.GetValue();
	return InspectFields(View.GetScriptStruct(), View.GetMemory());
}

TSharedPtr<FJsonObject> InspectNode(const FStateTreeEditorNode& Node, const FString& Kind, int32 Index)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("id"), Node.ID.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetStringField(TEXT("definition_id"), Node.GetNodeID().ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetStringField(TEXT("kind"), Kind);
	Result->SetNumberField(TEXT("index"), Index);
	Result->SetObjectField(TEXT("editor_node"), InspectFields(FStateTreeEditorNode::StaticStruct(), &Node,
		{TEXT("Node"), TEXT("Instance"), TEXT("InstanceObject"), TEXT("ExecutionRuntimeData"), TEXT("ExecutionRuntimeDataObject")}));
	Result->SetObjectField(TEXT("definition"), InspectFields(Node.Node.GetScriptStruct(), Node.Node.GetMemory()));
	Result->SetObjectField(TEXT("instance_struct"), InspectFields(Node.Instance.GetScriptStruct(), Node.Instance.GetMemory()));
	Result->SetObjectField(TEXT("execution_runtime_struct"), InspectFields(Node.ExecutionRuntimeData.GetScriptStruct(), Node.ExecutionRuntimeData.GetMemory()));
	for (const TPair<FString, const UObject*>& Object : TArray<TPair<FString, const UObject*>>{
		{TEXT("instance_object"), Node.InstanceObject.Get()}, {TEXT("execution_runtime_object"), Node.ExecutionRuntimeDataObject.Get()}})
	{
		TSharedPtr<FJsonObject> Values = InspectFields(Object.Value ? Object.Value->GetClass() : nullptr, Object.Value);
		if (Object.Value)
		{
			Values->SetStringField(TEXT("object_path"), Object.Value->GetPathName());
		}
		Result->SetObjectField(Object.Key, Values);
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> InspectNodes(const TArray<FStateTreeEditorNode>& Nodes, const FString& Kind)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		Result.Add(MakeShared<FJsonValueObject>(InspectNode(Nodes[Index], Kind, Index)));
	}
	return Result;
}

TSharedPtr<FJsonObject> InspectTree(const FCortexSTAssetContext& Context)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("schema"), TEXT("cortex.stored-editor-inspection.v1"));
	Result->SetStringField(TEXT("asset_path"), Context.AssetPath);
	Result->SetStringField(TEXT("scope"), TEXT("All stored editor subtrees; external linked assets are identity references only. No runtime execution or class-default substitution."));
	Result->SetStringField(TEXT("enabled_origin"), TEXT("definition.fields.bTaskEnabled where present; other node types have only their actual reflected flags, no synthetic enabled default"));
	Result->SetStringField(TEXT("completeness"), TEXT("Reflected stored fields with per-field partial/issues. UObject references identity-only; instance objects expanded one explicit level. Depth limit 32 reported by serializer, no array count truncation. Non-reflected engine caches not captured."));
	Result->SetBoolField(TEXT("package_dirty_before"), Context.StateTree->GetOutermost()->IsDirty());
	Result->SetObjectField(TEXT("root"), InspectFields(Context.EditorData->GetClass(), Context.EditorData,
		{TEXT("SubTrees"), TEXT("Evaluators"), TEXT("GlobalTasks"), TEXT("EditorBindings"), TEXT("RootParameterPropertyBag"), TEXT("RootParameters")}));
	Result->SetStringField(TEXT("root_parameters_id"), Context.EditorData->GetRootParametersGuid().ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetObjectField(TEXT("root_parameters"), InspectBag(Context.EditorData->GetRootParametersPropertyBag()));
	Result->SetArrayField(TEXT("evaluators"), InspectNodes(Context.EditorData->Evaluators, TEXT("evaluator")));
	Result->SetArrayField(TEXT("global_tasks"), InspectNodes(Context.EditorData->GlobalTasks, TEXT("global_task")));
	Result->SetObjectField(TEXT("bindings"), InspectFields(FStateTreeEditorPropertyBindings::StaticStruct(), &Context.EditorData->EditorBindings));
	TArray<TSharedPtr<FJsonValue>> States;
	TArray<TSharedPtr<FJsonValue>> Roots;
	TSet<const UStateTreeState*> Visited;
	for (int32 RootIndex = 0; RootIndex < Context.EditorData->SubTrees.Num(); ++RootIndex)
	{
		const UStateTreeState* Root = Context.EditorData->SubTrees[RootIndex];
		if (Root)
		{
			Roots.Add(MakeShared<FJsonValueString>(Root->ID.ToString(EGuidFormats::DigitsWithHyphens)));
		}
		else
		{
			Roots.Add(MakeShared<FJsonValueNull>());
		}
		TArray<const UStateTreeState*> Pending;
		Pending.Add(Root);
		while (!Pending.IsEmpty())
		{
			const UStateTreeState* State = Pending.Pop(EAllowShrinking::No);
			if (State == nullptr || Visited.Contains(State))
			{
				continue;
			}
			Visited.Add(State);
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("id"), State->ID.ToString(EGuidFormats::DigitsWithHyphens));
			Item->SetStringField(TEXT("object_path"), State->GetPathName());
			Item->SetNumberField(TEXT("subtree_index"), RootIndex);
			Item->SetObjectField(TEXT("properties"), InspectFields(State->GetClass(), State,
				{TEXT("Tasks"), TEXT("SingleTask"), TEXT("EnterConditions"), TEXT("Considerations"), TEXT("Transitions"), TEXT("Parameters")}));
			Item->SetObjectField(TEXT("parameters_metadata"), InspectFields(FStateTreeStateParameters::StaticStruct(), &State->Parameters, {TEXT("Parameters")}));
			Item->SetObjectField(TEXT("parameters"), InspectBag(State->Parameters.Parameters));
			Item->SetArrayField(TEXT("tasks"), InspectNodes(State->Tasks, TEXT("task")));
			Item->SetObjectField(TEXT("single_task"), InspectNode(State->SingleTask, TEXT("single_task"), 0));
			Item->SetArrayField(TEXT("enter_conditions"), InspectNodes(State->EnterConditions, TEXT("enter_condition")));
			Item->SetArrayField(TEXT("considerations"), InspectNodes(State->Considerations, TEXT("consideration")));
			TArray<TSharedPtr<FJsonValue>> Transitions;
			for (int32 Index = 0; Index < State->Transitions.Num(); ++Index)
			{
				const FStateTreeTransition& Transition = State->Transitions[Index];
				TSharedPtr<FJsonObject> Value = InspectFields(FStateTreeTransition::StaticStruct(), &Transition, {TEXT("Conditions")});
				Value->SetNumberField(TEXT("index"), Index);
				Value->SetArrayField(TEXT("conditions"), InspectNodes(Transition.Conditions, TEXT("transition_condition")));
				Transitions.Add(MakeShared<FJsonValueObject>(Value));
			}
			Item->SetArrayField(TEXT("transitions"), Transitions);
			States.Add(MakeShared<FJsonValueObject>(Item));
			for (int32 Index = State->Children.Num() - 1; Index >= 0; --Index)
			{
				Pending.Add(State->Children[Index]);
			}
		}
	}
	Result->SetArrayField(TEXT("subtree_roots"), Roots);
	Result->SetArrayField(TEXT("states"), States);
	Result->SetBoolField(TEXT("package_dirty_after"), Context.StateTree->GetOutermost()->IsDirty());
	return Result;
}
}

FCortexCommandResult FCortexSTInspectOps::DumpTree(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	FCortexCommandResult Error;
	if (!CortexST::GetRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error;
	}

	FCortexSTAssetContext Context;
	if (!CortexST::LoadAssetContext(AssetPath, Context, Error))
	{
		return Error;
	}

	if (CortexST::GetOptionalBool(Params, TEXT("inspect_instances"), false))
	{
		TSharedPtr<FJsonObject> Inspection = InspectTree(Context);
		FString Section;
		if (Params->TryGetStringField(TEXT("inspect_section"), Section))
		{
			TArray<TSharedPtr<FJsonValue>> Entries;
			if (Section == TEXT("root"))
			{
				TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
				Root->SetObjectField(TEXT("root"), Inspection->GetObjectField(TEXT("root")));
				Root->SetObjectField(TEXT("root_parameters"), Inspection->GetObjectField(TEXT("root_parameters")));
				Root->SetStringField(TEXT("root_parameters_id"), Inspection->GetStringField(TEXT("root_parameters_id")));
				Root->SetArrayField(TEXT("subtree_roots"), Inspection->GetArrayField(TEXT("subtree_roots")));
				Entries.Add(MakeShared<FJsonValueObject>(Root));
			}
			else if (Section == TEXT("bindings"))
			{
				for (const FStateTreePropertyPathBinding& Binding : Context.EditorData->EditorBindings.GetBindings())
				{
					Entries.Add(MakeShared<FJsonValueObject>(InspectFields(FStateTreePropertyPathBinding::StaticStruct(), &Binding)));
				}
			}
			else if (Section == TEXT("states") || Section == TEXT("nodes"))
			{
				for (const TSharedPtr<FJsonValue>& StateValue : Inspection->GetArrayField(TEXT("states")))
				{
					TSharedPtr<FJsonObject> State = StateValue->AsObject();
					if (Section == TEXT("nodes"))
					{
						for (const TCHAR* Kind : {TEXT("tasks"), TEXT("enter_conditions"), TEXT("considerations")})
						{
							for (const TSharedPtr<FJsonValue>& Node : State->GetArrayField(Kind))
							{
								Node->AsObject()->SetStringField(TEXT("owner_state_id"), State->GetStringField(TEXT("id")));
								Entries.Add(Node);
							}
						}
						const TSharedPtr<FJsonObject> Single = State->GetObjectField(TEXT("single_task"));
						if (Single->GetObjectField(TEXT("definition"))->GetBoolField(TEXT("available")))
						{
							Single->SetStringField(TEXT("owner_state_id"), State->GetStringField(TEXT("id")));
							Entries.Add(MakeShared<FJsonValueObject>(Single));
						}
						for (const TSharedPtr<FJsonValue>& Transition : State->GetArrayField(TEXT("transitions")))
						{
							for (const TSharedPtr<FJsonValue>& Node : Transition->AsObject()->GetArrayField(TEXT("conditions")))
							{
								Node->AsObject()->SetStringField(TEXT("owner_state_id"), State->GetStringField(TEXT("id")));
								Node->AsObject()->SetNumberField(TEXT("owner_transition_index"), Transition->AsObject()->GetNumberField(TEXT("index")));
								Entries.Add(Node);
							}
						}
						continue;
					}
					for (const TCHAR* Kind : {TEXT("tasks"), TEXT("enter_conditions"), TEXT("considerations"), TEXT("single_task")})
					{
						State->RemoveField(Kind);
					}
					for (const TSharedPtr<FJsonValue>& Transition : State->GetArrayField(TEXT("transitions")))
					{
						Transition->AsObject()->RemoveField(TEXT("conditions"));
					}
					Entries.Add(StateValue);
				}
				if (Section == TEXT("nodes"))
				{
					Entries.Append(Inspection->GetArrayField(TEXT("evaluators")));
					Entries.Append(Inspection->GetArrayField(TEXT("global_tasks")));
				}
			}
			else
			{
				return FCortexCommandRouter::Error(CortexErrorCodes::InvalidField, TEXT("inspect_section must be root, states, nodes or bindings"));
			}
			double OffsetValue = 0;
			double CountValue = 1;
			Params->TryGetNumberField(TEXT("inspect_offset"), OffsetValue);
			Params->TryGetNumberField(TEXT("inspect_count"), CountValue);
			if (!FMath::IsFinite(OffsetValue) || !FMath::IsFinite(CountValue) || OffsetValue < 0 || OffsetValue > Entries.Num()
				|| CountValue < 1 || CountValue > 100 || OffsetValue != FMath::FloorToDouble(OffsetValue) || CountValue != FMath::FloorToDouble(CountValue))
			{
				return FCortexCommandRouter::Error(CortexErrorCodes::InvalidField, TEXT("Inspection offset/count must be bounded nonnegative/positive integers"));
			}
			const int32 Offset = static_cast<int32>(OffsetValue);
			const int32 End = FMath::Min(Entries.Num(), Offset + static_cast<int32>(CountValue));
			TArray<TSharedPtr<FJsonValue>> Page;
			for (int32 Index = Offset; Index < End; ++Index)
			{
				Page.Add(Entries[Index]);
			}
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("asset_path"), Context.AssetPath);
			Data->SetStringField(TEXT("schema"), Inspection->GetStringField(TEXT("schema")));
			Data->SetStringField(TEXT("section"), Section);
			Data->SetStringField(TEXT("completeness"), Inspection->GetStringField(TEXT("completeness")));
			Data->SetNumberField(TEXT("total"), Entries.Num());
			Data->SetNumberField(TEXT("offset"), Offset);
			Data->SetNumberField(TEXT("returned_count"), Page.Num());
			Data->SetBoolField(TEXT("has_more"), End < Entries.Num());
			Data->SetBoolField(TEXT("package_dirty_before"), Inspection->GetBoolField(TEXT("package_dirty_before")));
			Data->SetBoolField(TEXT("package_dirty_after"), Inspection->GetBoolField(TEXT("package_dirty_after")));
			Data->SetArrayField(TEXT("entries"), Page);
			return FCortexCommandRouter::Success(Data);
		}
		return FCortexCommandRouter::Success(Inspection);
	}

	const bool bIncludeTransitions = CortexST::GetOptionalBool(Params, TEXT("include_transitions"), true);
	const bool bIncludeNodes = CortexST::GetOptionalBool(Params, TEXT("include_nodes"), false);

	TArray<FCortexSTStateRef> States;
	CortexST::CollectAllStates(Context, States);

	TArray<TSharedPtr<FJsonValue>> SerializedStates;
	SerializedStates.Reserve(States.Num());
	for (const FCortexSTStateRef& StateRef : States)
	{
		SerializedStates.Add(MakeShared<FJsonValueObject>(
			CortexST::SerializeState(StateRef, bIncludeTransitions, bIncludeNodes)));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Context.AssetPath);
	Data->SetArrayField(TEXT("states"), SerializedStates);
	Data->SetObjectField(TEXT("validation"), CortexST::BuildValidationPayload(Context.StateTree));
	Data->SetObjectField(TEXT("fingerprint"), CortexST::MakeFingerprint(Context.StateTree));
	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexSTInspectOps::GetState(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	FCortexCommandResult Error;
	if (!CortexST::GetRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error;
	}

	FCortexSTAssetContext Context;
	if (!CortexST::LoadAssetContext(AssetPath, Context, Error))
	{
		return Error;
	}

	FCortexSTStateRef StateRef;
	if (!CortexST::ResolveState(Context, Params, StateRef, Error))
	{
		return Error;
	}

	TSharedPtr<FJsonObject> Data = CortexST::SerializeState(StateRef, true, false);
	Data->SetStringField(TEXT("asset_path"), Context.AssetPath);
	Data->SetObjectField(TEXT("validation"), CortexST::BuildValidationPayload(Context.StateTree));
	Data->SetObjectField(TEXT("fingerprint"), CortexST::MakeFingerprint(Context.StateTree));
	return FCortexCommandRouter::Success(Data);
}
