#include "Misc/AutomationTest.h"

#include "CortexBPCommandHandler.h"
#include "CortexBPTestLiftActor.h"
#include "CortexTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
	// Fixtures here own dedicated packages so every read can be checked against a genuinely
	// compiled Blueprint without touching an on-disk asset or another test's transient fixture.
	FString TokenReadUniquePath(const TCHAR* Purpose)
	{
		return FString::Printf(TEXT("/Game/Temp/CortexBPTokenRead_%s_%s"),
			Purpose,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits).Right(8));
	}

	UBlueprint* CreateTokenReadBlueprint(UClass* ParentClass, const FString& UniquePath)
	{
		UPackage* Package = CreatePackage(*UniquePath);
		if (!Package)
		{
			return nullptr;
		}

		const FName BlueprintName(*FPackageName::GetShortName(UniquePath));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			Package,
			BlueprintName,
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass());
		if (!Blueprint)
		{
			Package->MarkAsGarbage();
			return nullptr;
		}

		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return Blueprint;
	}

	// Retires ONLY the dedicated package created for a token-read fixture. Generated classes and
	// CDOs keep their reflection metadata for the engine's own later GC pass; only public/standalone
	// flags and non-metadata objects are released. No global GC is requested here.
	void RetireTokenReadFixture(UBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return;
		}

		UPackage* Package = Blueprint->GetOutermost();
		if (!Package)
		{
			return;
		}

		ForEachObjectWithOuter(Package, [](UObject* Object)
		{
			Object->ClearFlags(RF_Public | RF_Standalone);
			if (!Object->IsA<UField>() || Object->IsA<UClass>())
			{
				Object->MarkAsGarbage();
			}
		});
		Package->MarkAsGarbage();
	}

	struct FTokenReadScopedFixture
	{
		UBlueprint* Blueprint = nullptr;
		FString AssetPath;

		FTokenReadScopedFixture() = default;
		FTokenReadScopedFixture(const FTokenReadScopedFixture&) = delete;
		FTokenReadScopedFixture& operator=(const FTokenReadScopedFixture&) = delete;

		~FTokenReadScopedFixture()
		{
			RetireTokenReadFixture(Blueprint);
		}

		UBlueprint* Create(UClass* ParentClass, const FString& UniquePath)
		{
			Blueprint = CreateTokenReadBlueprint(ParentClass, UniquePath);
			if (Blueprint)
			{
				AssetPath = Blueprint->GetPathName();
			}
			return Blueprint;
		}
	};

	AActor* TokenReadActorCDO(const FTokenReadScopedFixture& Fixture)
	{
		return Fixture.Blueprint && Fixture.Blueprint->GeneratedClass
			? Cast<AActor>(Fixture.Blueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
	}

	TSharedPtr<FJsonObject> TokenReadParams(const FString& AssetPath)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("asset_path"), AssetPath);
		return Params;
	}

	struct FTokenReadPagedSweep
	{
		TArray<FString> OrderedNames;
		TSet<FString> UniqueNames;
		int32 TotalProperties = -1;
		int32 SumReturned = 0;
		int32 PageCount = 0;
		bool bComplete = false;
		bool bHonestPages = true;
	};

	// Pages an admitted metadata population from offset 0 until the response declares completion.
	// Every structural fact the caller relies on is verified per page, so a duplicate, a skip or a
	// continuation that cannot advance is reported instead of being silently tolerated.
	FTokenReadPagedSweep TokenReadSweepMetadataPages(
		FCortexBPCommandHandler& Handler,
		const TCHAR* Command,
		const FString& AssetPath,
		int32 PageLimit,
		int32 MaxPages = 256)
	{
		FTokenReadPagedSweep Sweep;
		int32 Offset = 0;

		for (int32 Page = 0; Page < MaxPages; ++Page)
		{
			TSharedPtr<FJsonObject> Params = TokenReadParams(AssetPath);
			Params->SetNumberField(TEXT("discovery_offset"), Offset);
			Params->SetNumberField(TEXT("discovery_limit"), PageLimit);

			const FCortexCommandResult PageResult = Handler.Execute(Command, Params);
			if (!PageResult.bSuccess || !PageResult.Data.IsValid())
			{
				Sweep.bHonestPages = false;
				return Sweep;
			}

			if (Sweep.TotalProperties < 0)
			{
				double Total = 0.0;
				if (!PageResult.Data->TryGetNumberField(TEXT("total_properties"), Total))
				{
					Sweep.bHonestPages = false;
					return Sweep;
				}
				Sweep.TotalProperties = static_cast<int32>(Total);
			}

			const TSharedPtr<FJsonObject>* PageProperties = nullptr;
			if (!PageResult.Data->TryGetObjectField(TEXT("properties"), PageProperties) || !PageProperties)
			{
				Sweep.bHonestPages = false;
				return Sweep;
			}

			const TSharedPtr<FJsonObject>* Pagination = nullptr;
			if (!PageResult.Data->TryGetObjectField(TEXT("discovery_pagination"), Pagination) || !Pagination)
			{
				Sweep.bHonestPages = false;
				return Sweep;
			}

			double PageOffset = -1.0;
			double PageLimitEcho = -1.0;
			double PageReturned = -1.0;
			bool bPageComplete = false;
			const bool bReadable =
				(*Pagination)->TryGetNumberField(TEXT("offset"), PageOffset)
				&& (*Pagination)->TryGetNumberField(TEXT("limit"), PageLimitEcho)
				&& (*Pagination)->TryGetNumberField(TEXT("returned"), PageReturned)
				&& (*Pagination)->TryGetBoolField(TEXT("is_complete"), bPageComplete);

			const int32 ReturnedFields = (*PageProperties)->Values.Num();
			if (!bReadable
				|| static_cast<int32>(PageOffset) != Offset
				|| static_cast<int32>(PageLimitEcho) != PageLimit
				|| static_cast<int32>(PageReturned) != ReturnedFields
				|| ReturnedFields > PageLimit)
			{
				Sweep.bHonestPages = false;
				return Sweep;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PageProperties)->Values)
			{
				Sweep.OrderedNames.Add(Pair.Key);
				Sweep.UniqueNames.Add(Pair.Key);
			}
			Sweep.SumReturned += ReturnedFields;
			++Sweep.PageCount;

			if (bPageComplete)
			{
				Sweep.bComplete = true;
				return Sweep;
			}

			double NextOffset = -1.0;
			if (!(*Pagination)->TryGetNumberField(TEXT("next_offset"), NextOffset)
				|| static_cast<int32>(NextOffset) != Offset + ReturnedFields
				|| static_cast<int32>(NextOffset) <= Offset)
			{
				Sweep.bHonestPages = false;
				return Sweep;
			}
			Offset = static_cast<int32>(NextOffset);
		}

		// The cap was reached without the response ever declaring completion.
		Sweep.bHonestPages = false;
		return Sweep;
	}

	bool TokenReadSetsMatch(const TSet<FString>& A, const TSet<FString>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (const FString& Name : A)
		{
			if (!B.Contains(Name))
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexBPTokenReadMetadataOnlyTest,
	"Cortex.Blueprint.TokenRead.Get.MetadataOnlyDeclaresNoValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexBPTokenReadMetadataOnlyTest::RunTest(const FString&)
{
	FTokenReadScopedFixture Fixture;
	FCortexBPCommandHandler Handler;
	Fixture.Create(ACortexBPTestLiftActor::StaticClass(), TokenReadUniquePath(TEXT("Meta")));
	if (!TestNotNull(TEXT("TokenRead metadata fixture should be created"), Fixture.Blueprint))
	{
		return true;
	}

	const FCortexCommandResult Meta = Handler.Execute(TEXT("get_class_defaults"), TokenReadParams(Fixture.AssetPath));
	if (!TestTrue(TEXT("default metadata read should succeed"), Meta.bSuccess) || !Meta.Data.IsValid())
	{
		return true;
	}

	bool bValuesIncluded = true;
	TestTrue(TEXT("discovery declares value inclusion"),
		Meta.Data->TryGetBoolField(TEXT("values_included"), bValuesIncluded));
	TestFalse(TEXT("metadata does not claim values"), bValuesIncluded);

	FString ParentClass;
	TestTrue(TEXT("metadata should name the parent class"),
		Meta.Data->TryGetStringField(TEXT("parent_class"), ParentClass));
	TestEqual(TEXT("metadata parent_class should be the fixture parent"),
		ParentClass, FString(TEXT("CortexBPTestLiftActor")));

	const TSharedPtr<FJsonObject>* Properties = nullptr;
	if (!TestTrue(TEXT("metadata should expose a properties object"),
		Meta.Data->TryGetObjectField(TEXT("properties"), Properties) && Properties != nullptr))
	{
		return true;
	}
	TestTrue(TEXT("metadata should expose the admitted property population"), (*Properties)->Values.Num() > 0);

	// Metadata discovery must not extract reflected values, including for the heavy tick struct
	// and the fixture-owned component reference.
	int32 ValueFields = 0;
	int32 MemberFields = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
	{
		const TSharedPtr<FJsonObject> Entry = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
		if (!TestNotNull(TEXT("metadata entry should be an object"), Entry.Get()))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("metadata entry %s should keep its type"), *Pair.Key),
			Entry->HasField(TEXT("type")));
		if (Entry->HasField(TEXT("value")))
		{
			++ValueFields;
		}
		if (Entry->HasField(TEXT("members")))
		{
			++MemberFields;
		}
	}
	TestEqual(TEXT("metadata discovery must not serialize property values"), ValueFields, 0);
	TestEqual(TEXT("metadata discovery must not expand struct members"), MemberFields, 0);

	const TSharedPtr<FJsonObject>* MeshEntry = nullptr;
	TestTrue(TEXT("metadata should include the fixture component property"),
		(*Properties)->TryGetObjectField(TEXT("Mesh"), MeshEntry));
	const TSharedPtr<FJsonObject>* TickEntry = nullptr;
	TestTrue(TEXT("metadata should include the inherited tick struct property"),
		(*Properties)->TryGetObjectField(TEXT("PrimaryActorTick"), TickEntry));
	if (TickEntry && *TickEntry)
	{
		TestFalse(TEXT("the heavy tick struct must stay metadata-only"), (*TickEntry)->HasField(TEXT("value")));
	}

	double TotalProperties = 0.0;
	TestTrue(TEXT("metadata should declare total_properties"),
		Meta.Data->TryGetNumberField(TEXT("total_properties"), TotalProperties));
	TestTrue(TEXT("total_properties should cover the returned metadata"),
		TotalProperties > 0.0 && TotalProperties >= (*Properties)->Values.Num());

	const TSharedPtr<FJsonObject>* Pagination = nullptr;
	if (TestTrue(TEXT("metadata should declare discovery_pagination"),
		Meta.Data->TryGetObjectField(TEXT("discovery_pagination"), Pagination) && Pagination != nullptr))
	{
		double Offset = -1.0;
		double Limit = -1.0;
		double Returned = -1.0;
		bool bComplete = false;
		TestTrue(TEXT("pagination should declare its offset"), (*Pagination)->TryGetNumberField(TEXT("offset"), Offset));
		TestEqual(TEXT("pagination offset should default to 0"), Offset, 0.0);
		TestTrue(TEXT("pagination should declare its limit"), (*Pagination)->TryGetNumberField(TEXT("limit"), Limit));
		TestEqual(TEXT("pagination limit should default to 100"), static_cast<int32>(Limit), 100);
		TestTrue(TEXT("pagination should declare its returned count"),
			(*Pagination)->TryGetNumberField(TEXT("returned"), Returned));
		TestEqual(TEXT("pagination returned count should match the returned metadata"),
			static_cast<int32>(Returned), (*Properties)->Values.Num());
		TestTrue(TEXT("pagination should declare completeness"),
			(*Pagination)->TryGetBoolField(TEXT("is_complete"), bComplete));
		if (static_cast<int32>(TotalProperties) <= 100)
		{
			TestTrue(TEXT("a population inside the default page should be complete"), bComplete);
		}
	}

	// An explicitly empty selector is metadata discovery, not a value read.
	TSharedPtr<FJsonObject> EmptyParams = TokenReadParams(Fixture.AssetPath);
	EmptyParams->SetArrayField(TEXT("properties"), TArray<TSharedPtr<FJsonValue>>());
	const FCortexCommandResult EmptyMeta = Handler.Execute(TEXT("get_class_defaults"), EmptyParams);
	if (TestTrue(TEXT("empty selector read should succeed"), EmptyMeta.bSuccess) && EmptyMeta.Data.IsValid())
	{
		bool bEmptyValuesIncluded = true;
		TestTrue(TEXT("empty selector read declares value inclusion"),
			EmptyMeta.Data->TryGetBoolField(TEXT("values_included"), bEmptyValuesIncluded));
		TestFalse(TEXT("empty selector read must not claim values"), bEmptyValuesIncluded);
	}

	// An explicit include_values=false keeps the read metadata-only.
	TSharedPtr<FJsonObject> OptOutParams = TokenReadParams(Fixture.AssetPath);
	OptOutParams->SetBoolField(TEXT("include_values"), false);
	const FCortexCommandResult OptOut = Handler.Execute(TEXT("get_class_defaults"), OptOutParams);
	if (TestTrue(TEXT("explicit metadata-only read should succeed"), OptOut.bSuccess) && OptOut.Data.IsValid())
	{
		bool bOptOutValuesIncluded = true;
		TestTrue(TEXT("explicit metadata-only read declares value inclusion"),
			OptOut.Data->TryGetBoolField(TEXT("values_included"), bOptOutValuesIncluded));
		TestFalse(TEXT("explicit metadata-only read must not claim values"), bOptOutValuesIncluded);

		const TSharedPtr<FJsonObject>* OptOutProperties = nullptr;
		if (TestTrue(TEXT("explicit metadata-only read should expose properties"),
			OptOut.Data->TryGetObjectField(TEXT("properties"), OptOutProperties) && OptOutProperties != nullptr))
		{
			const TSharedPtr<FJsonObject>* OptOutTick = nullptr;
			if (TestTrue(TEXT("explicit metadata-only read should keep the tick struct"),
				(*OptOutProperties)->TryGetObjectField(TEXT("PrimaryActorTick"), OptOutTick) && OptOutTick != nullptr))
			{
				TestFalse(TEXT("explicit metadata-only read must not extract the struct value"),
					(*OptOutTick)->HasField(TEXT("value")));
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexBPTokenReadSelectedValuesTest,
	"Cortex.Blueprint.TokenRead.Get.SelectedEffectiveValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexBPTokenReadSelectedValuesTest::RunTest(const FString&)
{
	FTokenReadScopedFixture Fixture;
	FCortexBPCommandHandler Handler;
	Fixture.Create(ACortexBPTestLiftActor::StaticClass(), TokenReadUniquePath(TEXT("Select")));
	if (!TestNotNull(TEXT("TokenRead selection fixture should be created"), Fixture.Blueprint))
	{
		return true;
	}

	AActor* CDO = TokenReadActorCDO(Fixture);
	if (!TestNotNull(TEXT("TokenRead fixture should expose an actor CDO"), CDO))
	{
		return true;
	}
	TestFalse(TEXT("fixture actor should default to bReplicates=false"), CDO->GetIsReplicated());

	// Selected property reads are effective-value reads: false is a value, not an omission.
	TSharedPtr<FJsonObject> Params = TokenReadParams(Fixture.AssetPath);
	Params->SetArrayField(TEXT("properties"), {
		MakeShared<FJsonValueString>(TEXT("bReplicates"))});

	const FCortexCommandResult Value = Handler.Execute(TEXT("get_class_defaults"), Params);
	if (!TestTrue(TEXT("selected property read should succeed"), Value.bSuccess) || !Value.Data.IsValid())
	{
		return true;
	}

	bool bValuesIncluded = false;
	TestTrue(TEXT("selected read declares value inclusion"),
		Value.Data->TryGetBoolField(TEXT("values_included"), bValuesIncluded));
	TestTrue(TEXT("selected read includes values"), bValuesIncluded);

	const TSharedPtr<FJsonObject>* SelectedProperties = nullptr;
	if (TestTrue(TEXT("selected read should expose properties"),
		Value.Data->TryGetObjectField(TEXT("properties"), SelectedProperties) && SelectedProperties != nullptr))
	{
		const TSharedPtr<FJsonObject>* BoolEntry = nullptr;
		if (TestTrue(TEXT("selected read should return bReplicates"),
			(*SelectedProperties)->TryGetObjectField(TEXT("bReplicates"), BoolEntry) && BoolEntry != nullptr))
		{
			TestTrue(TEXT("bReplicates should carry a value field"), (*BoolEntry)->HasField(TEXT("value")));
			TestFalse(TEXT("false is a value, not omission"), (*BoolEntry)->GetBoolField(TEXT("value")));
			TestEqual(TEXT("bReplicates should echo the CDO value"),
				static_cast<int32>((*BoolEntry)->GetBoolField(TEXT("value"))),
				static_cast<int32>(CDO->GetIsReplicated()));
		}
	}

	// A struct leaf selector returns that leaf's exact effective value.
	TSharedPtr<FJsonObject> LeafParams = TokenReadParams(Fixture.AssetPath);
	LeafParams->SetArrayField(TEXT("properties"), {
		MakeShared<FJsonValueString>(TEXT("PrimaryActorTick.bCanEverTick"))});
	const FCortexCommandResult Leaf = Handler.Execute(TEXT("get_class_defaults"), LeafParams);
	if (TestTrue(TEXT("struct leaf read should succeed"), Leaf.bSuccess) && Leaf.Data.IsValid())
	{
		const TSharedPtr<FJsonObject>* LeafProperties = nullptr;
		if (TestTrue(TEXT("struct leaf read should expose properties"),
			Leaf.Data->TryGetObjectField(TEXT("properties"), LeafProperties) && LeafProperties != nullptr))
		{
			TestEqual(TEXT("struct leaf selection should return exactly the requested path"),
				(*LeafProperties)->Values.Num(), 1);

			const TSharedPtr<FJsonObject>* LeafEntry = nullptr;
			if (TestTrue(TEXT("struct leaf read should return the requested path"),
				(*LeafProperties)->TryGetObjectField(TEXT("PrimaryActorTick.bCanEverTick"), LeafEntry) && LeafEntry != nullptr))
			{
				TestTrue(TEXT("struct leaf should carry a value field"), (*LeafEntry)->HasField(TEXT("value")));
				TestEqual(TEXT("struct leaf should echo the CDO member value"),
					static_cast<int32>((*LeafEntry)->GetBoolField(TEXT("value"))),
					static_cast<int32>(CDO->PrimaryActorTick.bCanEverTick));
			}
		}
	}

	// Selected reads are not silently paged by discovery offsets or limits.
	TSharedPtr<FJsonObject> PagedParams = TokenReadParams(Fixture.AssetPath);
	PagedParams->SetArrayField(TEXT("properties"), {
		MakeShared<FJsonValueString>(TEXT("bReplicates")),
		MakeShared<FJsonValueString>(TEXT("PrimaryActorTick.bCanEverTick"))});
	PagedParams->SetNumberField(TEXT("discovery_offset"), 1);
	PagedParams->SetNumberField(TEXT("discovery_limit"), 1);
	const FCortexCommandResult Paged = Handler.Execute(TEXT("get_class_defaults"), PagedParams);
	if (TestTrue(TEXT("selected read carrying discovery paging fields should succeed"), Paged.bSuccess)
		&& Paged.Data.IsValid())
	{
		const TSharedPtr<FJsonObject>* PagedProperties = nullptr;
		if (TestTrue(TEXT("selected read should expose properties"),
			Paged.Data->TryGetObjectField(TEXT("properties"), PagedProperties) && PagedProperties != nullptr))
		{
			TestTrue(TEXT("selected reads must not be paged away by discovery_offset"),
				(*PagedProperties)->HasField(TEXT("bReplicates")));
			TestTrue(TEXT("selected reads must not be paged away by discovery_limit"),
				(*PagedProperties)->HasField(TEXT("PrimaryActorTick.bCanEverTick")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexBPTokenReadMembersOptInTest,
	"Cortex.Blueprint.TokenRead.Get.MembersOptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexBPTokenReadMembersOptInTest::RunTest(const FString&)
{
	FTokenReadScopedFixture Fixture;
	FCortexBPCommandHandler Handler;
	Fixture.Create(ACortexBPTestLiftActor::StaticClass(), TokenReadUniquePath(TEXT("Members")));
	if (!TestNotNull(TEXT("TokenRead members fixture should be created"), Fixture.Blueprint))
	{
		return true;
	}

	AActor* CDO = TokenReadActorCDO(Fixture);
	if (!TestNotNull(TEXT("TokenRead fixture should expose an actor CDO"), CDO))
	{
		return true;
	}

	// Default metadata discovery must not expand struct members.
	const FCortexCommandResult Meta = Handler.Execute(TEXT("get_class_defaults"), TokenReadParams(Fixture.AssetPath));
	if (!TestTrue(TEXT("default metadata read should succeed"), Meta.bSuccess) || !Meta.Data.IsValid())
	{
		return true;
	}

	const TSharedPtr<FJsonObject>* MetaProperties = nullptr;
	if (TestTrue(TEXT("default metadata read should expose properties"),
		Meta.Data->TryGetObjectField(TEXT("properties"), MetaProperties) && MetaProperties != nullptr))
	{
		const TSharedPtr<FJsonObject>* MetaTick = nullptr;
		if (TestTrue(TEXT("default metadata read should include the tick struct"),
			(*MetaProperties)->TryGetObjectField(TEXT("PrimaryActorTick"), MetaTick) && MetaTick != nullptr))
		{
			TestFalse(TEXT("struct member expansion must be opt-in"), (*MetaTick)->HasField(TEXT("members")));
		}
	}

	// include_members=true adds one-level member path/type metadata without values.
	TSharedPtr<FJsonObject> MemberParams = TokenReadParams(Fixture.AssetPath);
	MemberParams->SetBoolField(TEXT("include_members"), true);
	const FCortexCommandResult MemberRead = Handler.Execute(TEXT("get_class_defaults"), MemberParams);
	if (!TestTrue(TEXT("metadata read with include_members should succeed"), MemberRead.bSuccess)
		|| !MemberRead.Data.IsValid())
	{
		return true;
	}

	bool bMemberValuesIncluded = true;
	TestTrue(TEXT("member metadata read declares value inclusion"),
		MemberRead.Data->TryGetBoolField(TEXT("values_included"), bMemberValuesIncluded));
	TestFalse(TEXT("member metadata read must not claim values"), bMemberValuesIncluded);

	const TSharedPtr<FJsonObject>* MemberProperties = nullptr;
	if (TestTrue(TEXT("member metadata read should expose properties"),
		MemberRead.Data->TryGetObjectField(TEXT("properties"), MemberProperties) && MemberProperties != nullptr))
	{
		const TSharedPtr<FJsonObject>* MemberTick = nullptr;
		if (TestTrue(TEXT("member metadata read should include the tick struct"),
			(*MemberProperties)->TryGetObjectField(TEXT("PrimaryActorTick"), MemberTick) && MemberTick != nullptr))
		{
			TestFalse(TEXT("member metadata read must not extract the struct value"),
				(*MemberTick)->HasField(TEXT("value")));

			const TSharedPtr<FJsonObject>* Members = nullptr;
			if (TestTrue(TEXT("include_members should add one-level members"),
				(*MemberTick)->TryGetObjectField(TEXT("members"), Members) && Members != nullptr))
			{
				const TSharedPtr<FJsonObject>* MemberEntry = nullptr;
				if (TestTrue(TEXT("members should include the tick leaf"),
					(*Members)->TryGetObjectField(TEXT("bCanEverTick"), MemberEntry) && MemberEntry != nullptr))
				{
					FString Path;
					TestTrue(TEXT("member metadata should expose its dot path"),
						(*MemberEntry)->TryGetStringField(TEXT("path"), Path));
					TestEqual(TEXT("member path should be dot-notation"),
						Path, FString(TEXT("PrimaryActorTick.bCanEverTick")));
					TestTrue(TEXT("member metadata should expose its type"), (*MemberEntry)->HasField(TEXT("type")));
					TestFalse(TEXT("member values require include_values"), (*MemberEntry)->HasField(TEXT("value")));
				}
			}
		}
	}

	// include_values=true recovers the struct and member values for the same representation.
	TSharedPtr<FJsonObject> ValueParams = TokenReadParams(Fixture.AssetPath);
	ValueParams->SetBoolField(TEXT("include_members"), true);
	ValueParams->SetBoolField(TEXT("include_values"), true);
	const FCortexCommandResult ValueRead = Handler.Execute(TEXT("get_class_defaults"), ValueParams);
	if (!TestTrue(TEXT("member value read should succeed"), ValueRead.bSuccess) || !ValueRead.Data.IsValid())
	{
		return true;
	}

	bool bValueReadIncluded = false;
	TestTrue(TEXT("member value read declares value inclusion"),
		ValueRead.Data->TryGetBoolField(TEXT("values_included"), bValueReadIncluded));
	TestTrue(TEXT("member value read includes values"), bValueReadIncluded);

	const TSharedPtr<FJsonObject>* ValueProperties = nullptr;
	if (TestTrue(TEXT("member value read should expose properties"),
		ValueRead.Data->TryGetObjectField(TEXT("properties"), ValueProperties) && ValueProperties != nullptr))
	{
		const TSharedPtr<FJsonObject>* ValueTick = nullptr;
		if (TestTrue(TEXT("member value read should include the tick struct"),
			(*ValueProperties)->TryGetObjectField(TEXT("PrimaryActorTick"), ValueTick) && ValueTick != nullptr))
		{
			const TSharedPtr<FJsonObject>* ValueMembers = nullptr;
			if (TestTrue(TEXT("member value read should include members"),
				(*ValueTick)->TryGetObjectField(TEXT("members"), ValueMembers) && ValueMembers != nullptr))
			{
				const TSharedPtr<FJsonObject>* ValueMemberEntry = nullptr;
				if (TestTrue(TEXT("members should include the tick leaf"),
					(*ValueMembers)->TryGetObjectField(TEXT("bCanEverTick"), ValueMemberEntry) && ValueMemberEntry != nullptr))
				{
					TestTrue(TEXT("requested member should carry a value field"),
						(*ValueMemberEntry)->HasField(TEXT("value")));
					TestEqual(TEXT("requested member value should echo the CDO"),
						static_cast<int32>((*ValueMemberEntry)->GetBoolField(TEXT("value"))),
						static_cast<int32>(CDO->PrimaryActorTick.bCanEverTick));
				}
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexBPTokenReadInvalidControlsTest,
	"Cortex.Blueprint.TokenRead.Get.InvalidReadControlsRefuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexBPTokenReadInvalidControlsTest::RunTest(const FString&)
{
	FTokenReadScopedFixture Fixture;
	FCortexBPCommandHandler Handler;
	Fixture.Create(ACortexBPTestLiftActor::StaticClass(), TokenReadUniquePath(TEXT("Invalid")));
	if (!TestNotNull(TEXT("TokenRead refusal fixture should be created"), Fixture.Blueprint))
	{
		return true;
	}

	const auto ExpectRefusal = [this](const FString& What, const FCortexCommandResult& Result)
	{
		TestFalse(*FString::Printf(TEXT("%s should refuse"), *What), Result.bSuccess);
		TestEqual(*FString::Printf(TEXT("%s should refuse with INVALID_FIELD"), *What),
			Result.ErrorCode, CortexErrorCodes::InvalidField);
	};

	{
		TSharedPtr<FJsonObject> Params = TokenReadParams(Fixture.AssetPath);
		Params->SetArrayField(TEXT("properties"), { MakeShared<FJsonValueNumber>(3.0) });
		ExpectRefusal(TEXT("a non-string property selector"),
			Handler.Execute(TEXT("get_class_defaults"), Params));
	}

	{
		TSharedPtr<FJsonObject> Params = TokenReadParams(Fixture.AssetPath);
		Params->SetStringField(TEXT("include_values"), TEXT("yes"));
		ExpectRefusal(TEXT("a non-boolean include_values"),
			Handler.Execute(TEXT("get_class_defaults"), Params));
	}

	{
		TSharedPtr<FJsonObject> Params = TokenReadParams(Fixture.AssetPath);
		Params->SetNumberField(TEXT("include_values"), 1);
		ExpectRefusal(TEXT("a numeric include_values"),
			Handler.Execute(TEXT("get_class_defaults"), Params));
	}

	{
		TSharedPtr<FJsonObject> Params = TokenReadParams(Fixture.AssetPath);
		Params->SetStringField(TEXT("include_members"), TEXT("true"));
		ExpectRefusal(TEXT("a non-boolean include_members"),
			Handler.Execute(TEXT("get_class_defaults"), Params));
	}

	{
		TSharedPtr<FJsonObject> Params = TokenReadParams(Fixture.AssetPath);
		Params->SetStringField(TEXT("discovery_limit"), TEXT("many"));
		ExpectRefusal(TEXT("a non-numeric discovery_limit"),
			Handler.Execute(TEXT("get_class_defaults"), Params));
	}

	{
		TSharedPtr<FJsonObject> Params = TokenReadParams(Fixture.AssetPath);
		Params->SetNumberField(TEXT("discovery_limit"), 0);
		ExpectRefusal(TEXT("a zero discovery_limit"),
			Handler.Execute(TEXT("get_class_defaults"), Params));
	}

	{
		TSharedPtr<FJsonObject> Params = TokenReadParams(Fixture.AssetPath);
		Params->SetNumberField(TEXT("discovery_limit"), 201);
		ExpectRefusal(TEXT("an oversized discovery_limit"),
			Handler.Execute(TEXT("get_class_defaults"), Params));
	}

	{
		TSharedPtr<FJsonObject> Params = TokenReadParams(Fixture.AssetPath);
		Params->SetNumberField(TEXT("discovery_offset"), -1);
		ExpectRefusal(TEXT("a negative discovery_offset"),
			Handler.Execute(TEXT("get_class_defaults"), Params));
	}

	{
		TSharedPtr<FJsonObject> Params = TokenReadParams(Fixture.AssetPath);
		Params->SetNumberField(TEXT("discovery_offset"), 1.5);
		ExpectRefusal(TEXT("a fractional discovery_offset"),
			Handler.Execute(TEXT("get_class_defaults"), Params));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexBPTokenReadDiscoveryPagingTest,
	"Cortex.Blueprint.TokenRead.Discovery.SettableDefaultsPagesCoverEachNameOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexBPTokenReadDiscoveryPagingTest::RunTest(const FString&)
{
	FTokenReadScopedFixture Fixture;
	FCortexBPCommandHandler Handler;
	Fixture.Create(ACortexBPTestLiftActor::StaticClass(), TokenReadUniquePath(TEXT("Paging")));
	if (!TestNotNull(TEXT("TokenRead paging fixture should be created"), Fixture.Blueprint))
	{
		return true;
	}

	// Coarse pages (the largest admitted page size) establish the admitted population once.
	const FTokenReadPagedSweep Coarse = TokenReadSweepMetadataPages(
		Handler, TEXT("list_settable_defaults"), Fixture.AssetPath, 200);
	TestTrue(TEXT("coarse discovery pages should be structurally honest"), Coarse.bHonestPages);
	TestTrue(TEXT("coarse discovery sweep should reach completeness"), Coarse.bComplete);
	TestTrue(TEXT("coarse discovery sweep should expose the admitted population"), Coarse.TotalProperties > 0);
	TestEqual(TEXT("coarse paged names should not be duplicated"),
		Coarse.OrderedNames.Num(), Coarse.UniqueNames.Num());
	TestEqual(TEXT("coarse paged names should cover every admitted property"),
		Coarse.UniqueNames.Num(), Coarse.TotalProperties);
	TestEqual(TEXT("coarse pages should sum to the admitted population"),
		Coarse.SumReturned, Coarse.TotalProperties);

	// Fine pages must reproduce the same names with no duplicates and no skips.
	const FTokenReadPagedSweep Fine = TokenReadSweepMetadataPages(
		Handler, TEXT("list_settable_defaults"), Fixture.AssetPath, 7);
	TestTrue(TEXT("fine discovery pages should be structurally honest"), Fine.bHonestPages);
	TestTrue(TEXT("fine discovery sweep should reach completeness"), Fine.bComplete);
	TestEqual(TEXT("fine paged names should not be duplicated"),
		Fine.OrderedNames.Num(), Fine.UniqueNames.Num());
	TestEqual(TEXT("fine paged names should cover every admitted property"),
		Fine.UniqueNames.Num(), Fine.TotalProperties);
	TestTrue(TEXT("coarse and fine pages should expose the same admitted names"),
		TokenReadSetsMatch(Coarse.UniqueNames, Fine.UniqueNames));

	// The default read must not exceed its own page or claim completeness it cannot back up.
	const FCortexCommandResult DefaultPage =
		Handler.Execute(TEXT("list_settable_defaults"), TokenReadParams(Fixture.AssetPath));
	TestTrue(TEXT("default discovery read should succeed"), DefaultPage.bSuccess);
	if (DefaultPage.bSuccess && DefaultPage.Data.IsValid())
	{
		const TSharedPtr<FJsonObject>* DefaultProperties = nullptr;
		if (TestTrue(TEXT("default discovery read should expose property metadata"),
			DefaultPage.Data->TryGetObjectField(TEXT("properties"), DefaultProperties) && DefaultProperties != nullptr))
		{
			double DefaultTotal = 0.0;
			double DefaultReturned = 0.0;
			double DefaultLimit = -1.0;
			bool bDefaultComplete = true;
			const TSharedPtr<FJsonObject>* DefaultPagination = nullptr;
			if (TestTrue(TEXT("default discovery read should declare discovery_pagination"),
				DefaultPage.Data->TryGetObjectField(TEXT("discovery_pagination"), DefaultPagination)
					&& DefaultPagination != nullptr))
			{
				TestTrue(TEXT("default pagination should echo the default limit"),
					(*DefaultPagination)->TryGetNumberField(TEXT("limit"), DefaultLimit));
				TestEqual(TEXT("default discovery limit should be 100"), static_cast<int32>(DefaultLimit), 100);
				TestTrue(TEXT("default pagination should declare the returned count"),
					(*DefaultPagination)->TryGetNumberField(TEXT("returned"), DefaultReturned));
				TestEqual(TEXT("default pagination returned count should match the returned fields"),
					static_cast<int32>(DefaultReturned), (*DefaultProperties)->Values.Num());
				TestTrue(TEXT("default pagination should declare completeness"),
					(*DefaultPagination)->TryGetBoolField(TEXT("is_complete"), bDefaultComplete));
				TestTrue(TEXT("default pagination should declare the population size"),
					DefaultPage.Data->TryGetNumberField(TEXT("total_properties"), DefaultTotal));

				const int32 ExpectedReturned = FMath::Min(100, static_cast<int32>(DefaultTotal));
				TestEqual(TEXT("default discovery read should return the first page of the admitted population"),
					(*DefaultProperties)->Values.Num(), ExpectedReturned);
				if (static_cast<int32>(DefaultTotal) <= 100)
				{
					TestTrue(TEXT("a population inside the default page should be complete"), bDefaultComplete);
				}
				else
				{
					TestFalse(TEXT("an over-population default page must not claim completeness"), bDefaultComplete);
				}
			}
		}
	}

	// get_class_defaults discovery mode pages the same admitted metadata.
	const FTokenReadPagedSweep GetCoarse = TokenReadSweepMetadataPages(
		Handler, TEXT("get_class_defaults"), Fixture.AssetPath, 200);
	TestTrue(TEXT("get_class_defaults discovery pages should be structurally honest"), GetCoarse.bHonestPages);
	TestTrue(TEXT("get_class_defaults discovery sweep should reach completeness"), GetCoarse.bComplete);
	TestEqual(TEXT("get_class_defaults paged names should not be duplicated"),
		GetCoarse.OrderedNames.Num(), GetCoarse.UniqueNames.Num());
	TestEqual(TEXT("get_class_defaults paged names should cover every admitted property"),
		GetCoarse.UniqueNames.Num(), GetCoarse.TotalProperties);

	return true;
}
