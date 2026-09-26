#pragma once

#include "CoreMinimal.h"

#include "GameFramework/Actor.h"
#include "Blueprint/UserWidget.h"

#include "CortexGraphMigrationTestTypes.generated.h"

/**
 * Generic, test-only native fixtures for the migration and bounded-transfer coverage.
 *
 * The declarations below deliberately cover the signature dimensions the migration compatibility
 * diff must compare: a map pin (container kind plus map terminal type), an array pin (container
 * kind), const reference pins, and an out (reference) parameter plus a return value so one
 * declaration is event-shaped and the other is function-graph-shaped. No game asset is involved.
 */
/** A generic test-only interface: the bounded-transfer dependency inventory must report it. */
UINTERFACE(Blueprintable, MinimalAPI)
class UCortexGraphMigrationFixtureInterface : public UInterface
{
	GENERATED_BODY()
};

class ICortexGraphMigrationFixtureInterface
{
	GENERATED_BODY()

public:
	/**
	 * Interface-declared declaration the fixture implements, so the bounded-transfer dependency
	 * inventory has a real interface/member dependency to report without any game asset.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "CortexGraphMigrationTest")
	void OnInterfacePing(int32 Value);
};

UCLASS(Blueprintable)
class ACortexGraphMigrationFixtureActor : public AActor, public ICortexGraphMigrationFixtureInterface
{
	GENERATED_BODY()

public:
	/**
	 * Event-shaped inherited declaration: no outputs, so it is placeable as an event entry. Its
	 * pins are the map, the array, the const string and the object.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "CortexGraphMigrationTest")
	void OnPayload(const TMap<int32, float>& Payload, const TArray<int32>& Ids, const FString& Tag, AActor* Source);
	virtual void OnPayload_Implementation(const TMap<int32, float>& Payload, const TArray<int32>& Ids, const FString& Tag, AActor* Source)
	{
	}

	/**
	 * Function-graph-shaped inherited declaration: an out (reference) parameter and a return value,
	 * so its terminators are a function entry plus a function result.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "CortexGraphMigrationTest")
	int32 ComputeScore(const FString& Tag, TArray<int32>& OutIds);
	virtual int32 ComputeScore_Implementation(const FString& Tag, TArray<int32>& OutIds)
	{
		return 0;
	}

	/** Native implementation of the fixture interface declaration. */
	virtual void OnInterfacePing_Implementation(int32 Value) override
	{
		(void)Value;
	}
};

/** Generic Widget-derived native test fixtures for guarded event retirement. */
UCLASS(Blueprintable)
class UCortexGraphRetireLegacyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "CortexGraphMigrationTest")
	void OnLegacyAlpha(int32 Value);
	virtual void OnLegacyAlpha_Implementation(int32 Value) { (void)Value; }

	UFUNCTION(BlueprintNativeEvent, Category = "CortexGraphMigrationTest")
	void OnLegacyBeta(const FString& Value);
	virtual void OnLegacyBeta_Implementation(const FString& Value) { (void)Value; }

	UFUNCTION(BlueprintNativeEvent, Category = "CortexGraphMigrationTest")
	void OnRetainedEvent();
	virtual void OnRetainedEvent_Implementation() {}
};

UCLASS(Blueprintable)
class UCortexGraphRetireTargetWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "CortexGraphMigrationTest")
	void OnRetainedEvent();
	virtual void OnRetainedEvent_Implementation() {}
};

UCLASS(Blueprintable)
class UCortexGraphRetireCollisionTargetWidget : public UCortexGraphRetireTargetWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CortexGraphMigrationTest")
	int32 NativeCollision = 0;
};

/**
 * Native reparent target for guarded widget lifecycle retirement.
 *
 * A plain `UUserWidget` parent is not proof that native behaviour replaced a retired Blueprint
 * lifecycle body, so this target implements the widget lifecycle natively: the four `Native*` entry
 * points are overridden (they are plain virtuals, not UFUNCTIONs, so the fixture proves native
 * ownership through the Blueprint's parent class) and one unrelated cosmetic override event must
 * survive retirement.
 */
UCLASS(Blueprintable)
class UCortexGraphRetireNativeLifecycleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Unrelated cosmetic override retained next to the selected lifecycle entries. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "CortexGraphMigrationTest")
	void OnRetainedCosmeticEvent();
	virtual void OnRetainedCosmeticEvent_Implementation() {}

	/** Lifecycle replaced natively after the reparent, making the Blueprint body obsolete. */
	virtual void NativeOnInitialized() override {}
	virtual void NativePreConstruct() override {}
	virtual void NativeConstruct() override {}
	virtual void NativeDestruct() override {}
};

/**
 * Generic Widget-derived native fixture for guarded call-output rewiring.
 *
 * `LegacyQuestRequirements` is the historic declaration whose pass/fail result was an out parameter;
 * `UpdateFromQuestRequirements` is the migrated declaration whose result is the return value. A live
 * `UK2Node_CallFunction` rebuilt from one to the other therefore retains the old `bPassedRequirements`
 * output as an in-use orphan while the current `ReturnValue` output exists. `SetGateEnabled` is the
 * direct bool consumer the orphan output feeds and `SetGateCount` is an independent int consumer
 * reached through the engine's bool-to-int conversion, so one stale output really has two consumers.
 *
 * `LegacyGuestEmailRequirements` / `UpdateGuestEmailRequirements` add the second signature shape: the
 * historic out parameter becomes an input of the same name, so the same rebuild leaves a same-named
 * surviving input next to the in-use orphan output.
 */
UCLASS(Blueprintable)
class UCortexGraphRewireFixtureWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Historic declaration: the pass/fail result is a non-const out parameter. */
	UFUNCTION(BlueprintCallable, Category = "CortexGraphMigrationTest")
	void LegacyQuestRequirements(int32 Quest, bool& bPassedRequirements)
	{
		(void)Quest;
		bPassedRequirements = false;
	}

	/** Migrated declaration: the same result is now the return value. */
	UFUNCTION(BlueprintCallable, Category = "CortexGraphMigrationTest")
	bool UpdateFromQuestRequirements(int32 Quest, int32 Attempt)
	{
		(void)Quest;
		return Attempt > 0;
	}

	/**
	 * Historic declaration whose out parameter shares its name with the migrated input parameter.
	 *
	 * `LegacyGuestEmailRequirements` carried the verdict as an out parameter, while
	 * `UpdateGuestEmailRequirements` takes the same name as an input beside its new return value. The
	 * engine's own signature rebuild therefore leaves an in-use orphan output and a surviving input on
	 * the same call node under one name, which a direction-blind pin lookup cannot tell apart.
	 */
	UFUNCTION(BlueprintCallable, Category = "CortexGraphMigrationTest")
	void LegacyGuestEmailRequirements(const FString& Email, bool& bGuestEmailValid)
	{
		(void)Email;
		bGuestEmailValid = false;
	}

	/** Migrated declaration: the same name is now an input beside the new return value. */
	UFUNCTION(BlueprintCallable, Category = "CortexGraphMigrationTest")
	bool UpdateGuestEmailRequirements(const FString& Email, bool bGuestEmailValid)
	{
		return !Email.IsEmpty() && bGuestEmailValid;
	}

	/** Independent bool consumer of the gate result. */
	UFUNCTION(BlueprintCallable, Category = "CortexGraphMigrationTest")
	void SetGateEnabled(bool bEnabled)
	{
		(void)bEnabled;
	}

	/** Independent int consumer of the converted gate result. */
	UFUNCTION(BlueprintCallable, Category = "CortexGraphMigrationTest")
	void SetGateCount(int32 Count)
	{
		(void)Count;
	}
};
