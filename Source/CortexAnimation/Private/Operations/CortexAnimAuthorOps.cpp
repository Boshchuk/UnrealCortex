#include "Operations/CortexAnimAuthorOps.h"
#include "Operations/CortexAnimOpsUtils.h"
#include "CortexAnimationModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ScopedTransaction.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Curves/RichCurve.h"

namespace
{
	bool ReadPath(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, FString& Out, FCortexCommandResult& Err)
	{
		if (!Params.IsValid() || !Params->TryGetStringField(Field, Out) || Out.IsEmpty())
		{
			Err = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidField,
				FString::Printf(TEXT("Missing required param: %s"), Field));
			return false;
		}
		return true;
	}

	/** Read an optional FVector from a 3-element JSON array param; returns Fallback if absent/malformed. */
	FVector ReadVector(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, const FVector& Fallback)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Params.IsValid() && Params->TryGetArrayField(Field, Arr) && Arr && Arr->Num() == 3)
		{
			return FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
		}
		return Fallback;
	}
}
using CortexAnim::LoadTyped;

// ───────────────────────────── Notifies ─────────────────────────────

FCortexCommandResult FCortexAnimAuthorOps::AddNotify(const TSharedPtr<FJsonObject>& Params)
{
	FString Path, Name;
	FCortexCommandResult Err;
	if (!ReadPath(Params, TEXT("asset_path"), Path, Err)) { return Err; }
	if (!ReadPath(Params, TEXT("name"), Name, Err)) { return Err; }

	double Time = 0.0;
	if (!Params->TryGetNumberField(TEXT("time"), Time))
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::InvalidField, TEXT("Missing required param: time"));
	}
	double Duration = 0.0;
	Params->TryGetNumberField(TEXT("duration"), Duration);

	UAnimSequence* Seq = LoadTyped<UAnimSequence>(Path);
	if (Seq == nullptr)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("AnimSequence not found: %s"), *Path));
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Cortex: Add Anim Notify")));
	Seq->Modify();

	FAnimNotifyEvent NewEvent;
	NewEvent.NotifyName = FName(*Name);
	NewEvent.Link(Seq, static_cast<float>(Time));
	NewEvent.TriggerTimeOffset = GetTriggerTimeOffsetForType(Seq->CalculateOffsetForNotify(static_cast<float>(Time)));
	if (Duration > 0.0)
	{
		NewEvent.SetDuration(static_cast<float>(Duration));
	}
	Seq->Notifies.Add(NewEvent);
	Seq->PostEditChange();
	Seq->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), Name);
	Data->SetNumberField(TEXT("time"), Time);
	Data->SetNumberField(TEXT("notify_count"), Seq->Notifies.Num());
	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexAnimAuthorOps::RemoveNotify(const TSharedPtr<FJsonObject>& Params)
{
	FString Path, Name;
	FCortexCommandResult Err;
	if (!ReadPath(Params, TEXT("asset_path"), Path, Err)) { return Err; }
	if (!ReadPath(Params, TEXT("name"), Name, Err)) { return Err; }

	UAnimSequence* Seq = LoadTyped<UAnimSequence>(Path);
	if (Seq == nullptr)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("AnimSequence not found: %s"), *Path));
	}

	const FName NotifyName(*Name);
	const int32 RemovedCount = Seq->Notifies.RemoveAll(
		[NotifyName](const FAnimNotifyEvent& Event) { return Event.NotifyName == NotifyName; });
	if (RemovedCount == 0)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::SymbolNotFound,
			FString::Printf(TEXT("No notify named '%s' on %s"), *Name, *Path));
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Cortex: Remove Anim Notify")));
	Seq->Modify();
	Seq->PostEditChange();
	Seq->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("removed"), RemovedCount);
	Data->SetNumberField(TEXT("notify_count"), Seq->Notifies.Num());
	return FCortexCommandRouter::Success(Data);
}

// ───────────────────────────── Curves ─────────────────────────────

FCortexCommandResult FCortexAnimAuthorOps::AddCurve(const TSharedPtr<FJsonObject>& Params)
{
	FString Path, CurveName;
	FCortexCommandResult Err;
	if (!ReadPath(Params, TEXT("asset_path"), Path, Err)) { return Err; }
	if (!ReadPath(Params, TEXT("curve_name"), CurveName, Err)) { return Err; }

	UAnimSequence* Seq = LoadTyped<UAnimSequence>(Path);
	if (Seq == nullptr)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("AnimSequence not found: %s"), *Path));
	}

	IAnimationDataController& Controller = Seq->GetController();
	const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
	const bool bAdded = Controller.AddCurve(CurveId, AACF_DefaultCurve, /*bShouldTransact*/ true);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("curve_name"), CurveName);
	Data->SetBoolField(TEXT("added"), bAdded);
	Data->SetStringField(TEXT("note"), bAdded ? TEXT("created") : TEXT("already existed"));
	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexAnimAuthorOps::SetCurveKeys(const TSharedPtr<FJsonObject>& Params)
{
	FString Path, CurveName;
	FCortexCommandResult Err;
	if (!ReadPath(Params, TEXT("asset_path"), Path, Err)) { return Err; }
	if (!ReadPath(Params, TEXT("curve_name"), CurveName, Err)) { return Err; }

	const TArray<TSharedPtr<FJsonValue>>* KeysJson = nullptr;
	if (!Params->TryGetArrayField(TEXT("keys"), KeysJson) || KeysJson == nullptr)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::InvalidField,
			TEXT("Missing required param: keys (array of {time, value})"));
	}

	UAnimSequence* Seq = LoadTyped<UAnimSequence>(Path);
	if (Seq == nullptr)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("AnimSequence not found: %s"), *Path));
	}

	TArray<FRichCurveKey> Keys;
	Keys.Reserve(KeysJson->Num());
	for (const TSharedPtr<FJsonValue>& Entry : *KeysJson)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (Entry.IsValid() && Entry->TryGetObject(Obj) && Obj)
		{
			double KeyTime = 0.0, KeyValue = 0.0;
			(*Obj)->TryGetNumberField(TEXT("time"), KeyTime);
			(*Obj)->TryGetNumberField(TEXT("value"), KeyValue);
			Keys.Add(FRichCurveKey(static_cast<float>(KeyTime), static_cast<float>(KeyValue)));
		}
	}
	if (Keys.Num() == 0)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::InvalidValue,
			TEXT("keys array contained no valid {time, value} entries"));
	}

	IAnimationDataController& Controller = Seq->GetController();
	const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
	// Ensure the curve exists, then replace its keys (one undo step via bShouldTransact).
	Controller.AddCurve(CurveId, AACF_DefaultCurve, /*bShouldTransact*/ true);
	const bool bOk = Controller.SetCurveKeys(CurveId, Keys, /*bShouldTransact*/ true);
	if (!bOk)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::InvalidOperation,
			FString::Printf(TEXT("Failed to set keys on curve '%s'"), *CurveName));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("curve_name"), CurveName);
	Data->SetNumberField(TEXT("key_count"), Keys.Num());
	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexAnimAuthorOps::RemoveCurve(const TSharedPtr<FJsonObject>& Params)
{
	FString Path, CurveName;
	FCortexCommandResult Err;
	if (!ReadPath(Params, TEXT("asset_path"), Path, Err)) { return Err; }
	if (!ReadPath(Params, TEXT("curve_name"), CurveName, Err)) { return Err; }

	UAnimSequence* Seq = LoadTyped<UAnimSequence>(Path);
	if (Seq == nullptr)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("AnimSequence not found: %s"), *Path));
	}

	IAnimationDataController& Controller = Seq->GetController();
	const FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
	const bool bRemoved = Controller.RemoveCurve(CurveId, /*bShouldTransact*/ true);
	if (!bRemoved)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::SymbolNotFound,
			FString::Printf(TEXT("Curve '%s' not found on %s"), *CurveName, *Path));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("curve_name"), CurveName);
	Data->SetBoolField(TEXT("removed"), true);
	return FCortexCommandRouter::Success(Data);
}

// ───────────────────────────── Montage sections ─────────────────────────────

FCortexCommandResult FCortexAnimAuthorOps::AddMontageSection(const TSharedPtr<FJsonObject>& Params)
{
	FString Path, Name;
	FCortexCommandResult Err;
	if (!ReadPath(Params, TEXT("asset_path"), Path, Err)) { return Err; }
	if (!ReadPath(Params, TEXT("name"), Name, Err)) { return Err; }

	double StartTime = 0.0;
	Params->TryGetNumberField(TEXT("start_time"), StartTime);

	UAnimMontage* Montage = LoadTyped<UAnimMontage>(Path);
	if (Montage == nullptr)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("AnimMontage not found: %s"), *Path));
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Cortex: Add Montage Section")));
	Montage->Modify();
	const int32 NewIndex = Montage->AddAnimCompositeSection(FName(*Name), static_cast<float>(StartTime));
	if (NewIndex == INDEX_NONE)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::AssetAlreadyExists,
			FString::Printf(TEXT("Section name '%s' is not unique on %s"), *Name, *Path));
	}
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), Name);
	Data->SetNumberField(TEXT("index"), NewIndex);
	Data->SetNumberField(TEXT("section_count"), Montage->CompositeSections.Num());
	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexAnimAuthorOps::RemoveMontageSection(const TSharedPtr<FJsonObject>& Params)
{
	FString Path, Name;
	FCortexCommandResult Err;
	if (!ReadPath(Params, TEXT("asset_path"), Path, Err)) { return Err; }
	if (!ReadPath(Params, TEXT("name"), Name, Err)) { return Err; }

	UAnimMontage* Montage = LoadTyped<UAnimMontage>(Path);
	if (Montage == nullptr)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("AnimMontage not found: %s"), *Path));
	}

	const int32 Index = Montage->GetSectionIndex(FName(*Name));
	if (Index == INDEX_NONE)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::SymbolNotFound,
			FString::Printf(TEXT("Section '%s' not found on %s"), *Name, *Path));
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Cortex: Remove Montage Section")));
	Montage->Modify();
	const bool bOk = Montage->DeleteAnimCompositeSection(Index);
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), Name);
	Data->SetBoolField(TEXT("removed"), bOk);
	Data->SetNumberField(TEXT("section_count"), Montage->CompositeSections.Num());
	return FCortexCommandRouter::Success(Data);
}

// ───────────────────────────── Skeleton sockets ─────────────────────────────

FCortexCommandResult FCortexAnimAuthorOps::AddSocket(const TSharedPtr<FJsonObject>& Params)
{
	FString Path, SocketName, BoneName;
	FCortexCommandResult Err;
	if (!ReadPath(Params, TEXT("skeleton_path"), Path, Err)) { return Err; }
	if (!ReadPath(Params, TEXT("socket_name"), SocketName, Err)) { return Err; }
	if (!ReadPath(Params, TEXT("bone_name"), BoneName, Err)) { return Err; }

	USkeleton* Skeleton = LoadTyped<USkeleton>(Path);
	if (Skeleton == nullptr)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("Skeleton not found: %s"), *Path));
	}

	const FName SocketFName(*SocketName);
	for (const USkeletalMeshSocket* Existing : Skeleton->Sockets)
	{
		if (Existing && Existing->SocketName == SocketFName)
		{
			return FCortexCommandRouter::Error(CortexErrorCodes::AssetAlreadyExists,
				FString::Printf(TEXT("Socket '%s' already exists on %s"), *SocketName, *Path));
		}
	}

	const FName BoneFName(*BoneName);
	if (Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneFName) == INDEX_NONE)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::SymbolNotFound,
			FString::Printf(TEXT("Bone '%s' not found on skeleton %s"), *BoneName, *Path));
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Cortex: Add Skeleton Socket")));
	Skeleton->Modify();

	USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Skeleton);
	Socket->SocketName = SocketFName;
	Socket->BoneName = BoneFName;
	Socket->RelativeLocation = ReadVector(Params, TEXT("location"), FVector::ZeroVector);
	const FVector Rot = ReadVector(Params, TEXT("rotation"), FVector::ZeroVector);
	Socket->RelativeRotation = FRotator(Rot.Y, Rot.Z, Rot.X); // pitch, yaw, roll
	Socket->RelativeScale = ReadVector(Params, TEXT("scale"), FVector::OneVector);
	Skeleton->Sockets.Add(Socket);
	Skeleton->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("socket_name"), SocketName);
	Data->SetStringField(TEXT("bone_name"), BoneName);
	Data->SetNumberField(TEXT("socket_count"), Skeleton->Sockets.Num());
	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexAnimAuthorOps::SetSocketTransform(const TSharedPtr<FJsonObject>& Params)
{
	FString Path, SocketName;
	FCortexCommandResult Err;
	if (!ReadPath(Params, TEXT("skeleton_path"), Path, Err)) { return Err; }
	if (!ReadPath(Params, TEXT("socket_name"), SocketName, Err)) { return Err; }

	USkeleton* Skeleton = LoadTyped<USkeleton>(Path);
	if (Skeleton == nullptr)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("Skeleton not found: %s"), *Path));
	}

	const FName SocketFName(*SocketName);
	USkeletalMeshSocket* Socket = nullptr;
	for (USkeletalMeshSocket* Existing : Skeleton->Sockets)
	{
		if (Existing && Existing->SocketName == SocketFName)
		{
			Socket = Existing;
			break;
		}
	}
	if (Socket == nullptr)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::SymbolNotFound,
			FString::Printf(TEXT("Socket '%s' not found on %s"), *SocketName, *Path));
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Cortex: Set Socket Transform")));
	Skeleton->Modify();
	Socket->Modify();
	Socket->RelativeLocation = ReadVector(Params, TEXT("location"), Socket->RelativeLocation);
	const FVector ExistingRot(Socket->RelativeRotation.Roll, Socket->RelativeRotation.Pitch, Socket->RelativeRotation.Yaw);
	const FVector Rot = ReadVector(Params, TEXT("rotation"), ExistingRot);
	Socket->RelativeRotation = FRotator(Rot.Y, Rot.Z, Rot.X); // pitch, yaw, roll
	Socket->RelativeScale = ReadVector(Params, TEXT("scale"), Socket->RelativeScale);
	Skeleton->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("socket_name"), SocketName);
	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexAnimAuthorOps::RemoveSocket(const TSharedPtr<FJsonObject>& Params)
{
	FString Path, SocketName;
	FCortexCommandResult Err;
	if (!ReadPath(Params, TEXT("skeleton_path"), Path, Err)) { return Err; }
	if (!ReadPath(Params, TEXT("socket_name"), SocketName, Err)) { return Err; }

	USkeleton* Skeleton = LoadTyped<USkeleton>(Path);
	if (Skeleton == nullptr)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::AnimationNotFound,
			FString::Printf(TEXT("Skeleton not found: %s"), *Path));
	}

	const FName SocketFName(*SocketName);
	const int32 RemovedCount = Skeleton->Sockets.RemoveAll(
		[SocketFName](const USkeletalMeshSocket* Socket) { return Socket && Socket->SocketName == SocketFName; });
	if (RemovedCount == 0)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::SymbolNotFound,
			FString::Printf(TEXT("Socket '%s' not found on %s"), *SocketName, *Path));
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Cortex: Remove Skeleton Socket")));
	Skeleton->Modify();
	Skeleton->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("socket_name"), SocketName);
	Data->SetNumberField(TEXT("removed"), RemovedCount);
	Data->SetNumberField(TEXT("socket_count"), Skeleton->Sockets.Num());
	return FCortexCommandRouter::Success(Data);
}
