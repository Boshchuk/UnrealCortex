#include "Operations/CortexAnimInspectOps.h"
#include "CortexAnimationModule.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/AnimBlueprint.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNode.h"

namespace
{
	/** Read the required "asset_path" param. Returns false + fills OutError on miss. */
	bool ReadAssetPath(const TSharedPtr<FJsonObject>& Params, FString& OutPath, FCortexCommandResult& OutError)
	{
		if (!Params.IsValid() || !Params->TryGetStringField(TEXT("asset_path"), OutPath) || OutPath.IsEmpty())
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidField,
				TEXT("Missing required param: asset_path"));
			return false;
		}
		return true;
	}

	/** Load an asset of type T by object path, or null. */
	template <typename T>
	T* LoadTyped(const FString& Path)
	{
		return LoadObject<T>(nullptr, *Path);
	}
}

FCortexCommandResult FCortexAnimInspectOps::ListAssets(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetType = TEXT("all");
	FString PathFilter;
	FString Query;
	int32 Limit = 100;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("asset_type"), AssetType);
		Params->TryGetStringField(TEXT("path_filter"), PathFilter);
		Params->TryGetStringField(TEXT("query"), Query);
		int32 ParamLimit = 0;
		if (Params->TryGetNumberField(TEXT("limit"), ParamLimit) && ParamLimit > 0)
		{
			Limit = ParamLimit;
		}
	}

	IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
	if (AssetRegistry == nullptr)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::EditorNotReady, TEXT("AssetRegistry is not available"));
	}
	if (AssetRegistry->IsLoadingAssets())
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::EditorNotReady,
			TEXT("Asset Registry scan in progress - retry in a few seconds"));
	}

	FARFilter Filter;
	Filter.bRecursiveClasses = true;
	auto AddClass = [&Filter](UClass* Cls) { Filter.ClassPaths.Add(Cls->GetClassPathName()); };
	const FString TypeLower = AssetType.ToLower();
	if (TypeLower == TEXT("sequence") || TypeLower == TEXT("all")) { AddClass(UAnimSequence::StaticClass()); }
	if (TypeLower == TEXT("montage") || TypeLower == TEXT("all")) { AddClass(UAnimMontage::StaticClass()); }
	if (TypeLower == TEXT("skeleton") || TypeLower == TEXT("all")) { AddClass(USkeleton::StaticClass()); }
	if (TypeLower == TEXT("animbp") || TypeLower == TEXT("all")) { AddClass(UAnimBlueprint::StaticClass()); }
	if (Filter.ClassPaths.Num() == 0)
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::InvalidValue,
			TEXT("asset_type must be one of: sequence | montage | animbp | skeleton | all"));
	}
	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*PathFilter));
		Filter.bRecursivePaths = true;
	}

	TArray<FAssetData> AssetDataList;
	AssetRegistry->GetAssets(Filter, AssetDataList);

	TArray<TSharedPtr<FJsonValue>> ResultArray;
	int32 Count = 0;
	for (const FAssetData& AssetData : AssetDataList)
	{
		if (Count >= Limit)
		{
			break;
		}
		const FString AssetName = AssetData.AssetName.ToString();
		const FString AssetPath = AssetData.GetObjectPathString();
		if (!Query.IsEmpty()
			&& !AssetName.Contains(Query, ESearchCase::IgnoreCase)
			&& !AssetPath.Contains(Query, ESearchCase::IgnoreCase))
		{
			continue;
		}
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), AssetName);
		Entry->SetStringField(TEXT("path"), AssetPath);
		Entry->SetStringField(TEXT("class_name"), AssetData.AssetClassPath.GetAssetName().ToString());
		ResultArray.Add(MakeShared<FJsonValueObject>(Entry));
		++Count;
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("assets"), ResultArray);
	Data->SetNumberField(TEXT("count"), ResultArray.Num());
	Data->SetNumberField(TEXT("total_before_limit"), AssetDataList.Num());
	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexAnimInspectOps::GetSequenceInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString Path;
	FCortexCommandResult Err;
	if (!ReadAssetPath(Params, Path, Err)) { return Err; }

	UAnimSequence* Seq = LoadTyped<UAnimSequence>(Path);
	if (Seq == nullptr)
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("AnimSequence not found or wrong type: %s"), *Path));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), Path);
	Data->SetNumberField(TEXT("play_length"), Seq->GetPlayLength());
	Data->SetNumberField(TEXT("num_sampled_keys"), Seq->GetNumberOfSampledKeys());

	const FFrameRate FrameRate = Seq->GetSamplingFrameRate();
	Data->SetNumberField(TEXT("frame_rate_numerator"), FrameRate.Numerator);
	Data->SetNumberField(TEXT("frame_rate_denominator"), FrameRate.Denominator);
	Data->SetNumberField(TEXT("frame_rate_fps"), FrameRate.AsDecimal());

	if (const USkeleton* Skeleton = Seq->GetSkeleton())
	{
		Data->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	}
	else
	{
		Data->SetStringField(TEXT("skeleton"), TEXT(""));
	}

	TArray<TSharedPtr<FJsonValue>> NotifyArray;
	for (const FAnimNotifyEvent& Notify : Seq->Notifies)
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("name"), Notify.NotifyName.ToString());
		Item->SetNumberField(TEXT("time"), Notify.GetTime());
		Item->SetNumberField(TEXT("duration"), Notify.GetDuration());
		Item->SetBoolField(TEXT("is_state"), Notify.NotifyStateClass != nullptr);
		NotifyArray.Add(MakeShared<FJsonValueObject>(Item));
	}
	Data->SetArrayField(TEXT("notifies"), NotifyArray);
	Data->SetNumberField(TEXT("notify_count"), NotifyArray.Num());

	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexAnimInspectOps::GetMontageInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString Path;
	FCortexCommandResult Err;
	if (!ReadAssetPath(Params, Path, Err)) { return Err; }

	UAnimMontage* Montage = LoadTyped<UAnimMontage>(Path);
	if (Montage == nullptr)
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("AnimMontage not found or wrong type: %s"), *Path));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), Path);
	Data->SetNumberField(TEXT("play_length"), Montage->GetPlayLength());
	if (const USkeleton* Skeleton = Montage->GetSkeleton())
	{
		Data->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	}

	TArray<TSharedPtr<FJsonValue>> SectionArray;
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("name"), Section.SectionName.ToString());
		Item->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());
		Item->SetNumberField(TEXT("start_time"), Section.GetTime());
		SectionArray.Add(MakeShared<FJsonValueObject>(Item));
	}
	Data->SetArrayField(TEXT("sections"), SectionArray);

	TArray<TSharedPtr<FJsonValue>> SlotArray;
	for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
	{
		SlotArray.Add(MakeShared<FJsonValueString>(Slot.SlotName.ToString()));
	}
	Data->SetArrayField(TEXT("slots"), SlotArray);

	TArray<TSharedPtr<FJsonValue>> NotifyArray;
	for (const FAnimNotifyEvent& Notify : Montage->Notifies)
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("name"), Notify.NotifyName.ToString());
		Item->SetNumberField(TEXT("time"), Notify.GetTime());
		Item->SetNumberField(TEXT("duration"), Notify.GetDuration());
		NotifyArray.Add(MakeShared<FJsonValueObject>(Item));
	}
	Data->SetArrayField(TEXT("notifies"), NotifyArray);

	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexAnimInspectOps::GetSkeletonInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString Path;
	FCortexCommandResult Err;
	if (!ReadAssetPath(Params, Path, Err)) { return Err; }

	USkeleton* Skeleton = LoadTyped<USkeleton>(Path);
	if (Skeleton == nullptr)
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("Skeleton not found or wrong type: %s"), *Path));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), Path);

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	TArray<TSharedPtr<FJsonValue>> BoneArray;
	const int32 NumBones = RefSkeleton.GetNum();
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetNumberField(TEXT("index"), BoneIndex);
		Item->SetStringField(TEXT("name"), RefSkeleton.GetBoneName(BoneIndex).ToString());
		Item->SetNumberField(TEXT("parent_index"), RefSkeleton.GetParentIndex(BoneIndex));
		BoneArray.Add(MakeShared<FJsonValueObject>(Item));
	}
	Data->SetArrayField(TEXT("bones"), BoneArray);
	Data->SetNumberField(TEXT("bone_count"), NumBones);

	TArray<TSharedPtr<FJsonValue>> SocketArray;
	for (const USkeletalMeshSocket* Socket : Skeleton->Sockets)
	{
		if (Socket == nullptr)
		{
			continue;
		}
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("name"), Socket->SocketName.ToString());
		Item->SetStringField(TEXT("bone"), Socket->BoneName.ToString());
		SocketArray.Add(MakeShared<FJsonValueObject>(Item));
	}
	Data->SetArrayField(TEXT("sockets"), SocketArray);
	Data->SetNumberField(TEXT("socket_count"), SocketArray.Num());

	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexAnimInspectOps::GetAnimBlueprintInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString Path;
	FCortexCommandResult Err;
	if (!ReadAssetPath(Params, Path, Err)) { return Err; }

	UAnimBlueprint* AnimBP = LoadTyped<UAnimBlueprint>(Path);
	if (AnimBP == nullptr)
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("AnimBlueprint not found or wrong type: %s"), *Path));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), Path);
	if (const USkeleton* Skeleton = AnimBP->TargetSkeleton)
	{
		Data->SetStringField(TEXT("target_skeleton"), Skeleton->GetPathName());
	}
	if (const UClass* ParentClass = AnimBP->ParentClass)
	{
		Data->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
	}

	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);

	TArray<TSharedPtr<FJsonValue>> MachineArray;
	for (const UEdGraph* Graph : AllGraphs)
	{
		if (Graph == nullptr)
		{
			continue;
		}
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
			if (StateMachineNode == nullptr)
			{
				continue;
			}
			const UAnimationStateMachineGraph* MachineGraph = StateMachineNode->EditorStateMachineGraph;
			if (MachineGraph == nullptr)
			{
				continue;
			}

			TSharedRef<FJsonObject> MachineObj = MakeShared<FJsonObject>();
			MachineObj->SetStringField(TEXT("name"), MachineGraph->GetName());

			TArray<TSharedPtr<FJsonValue>> StateArray;
			for (const UEdGraphNode* MachineNode : MachineGraph->Nodes)
			{
				if (const UAnimStateNode* StateNode = Cast<UAnimStateNode>(MachineNode))
				{
					StateArray.Add(MakeShared<FJsonValueString>(StateNode->GetStateName()));
				}
			}
			MachineObj->SetArrayField(TEXT("states"), StateArray);
			MachineObj->SetNumberField(TEXT("state_count"), StateArray.Num());
			MachineArray.Add(MakeShared<FJsonValueObject>(MachineObj));
		}
	}
	Data->SetArrayField(TEXT("state_machines"), MachineArray);
	Data->SetNumberField(TEXT("state_machine_count"), MachineArray.Num());

	return FCortexCommandRouter::Success(Data);
}
