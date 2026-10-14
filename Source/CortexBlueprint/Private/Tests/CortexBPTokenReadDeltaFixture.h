#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "CortexBPTokenReadDeltaFixture.generated.h"

/**
 * Dedicated native parent fixture for the ownership-safe inherited-default delta regression.
 *
 * The parent CDO owns bReplicates=true plus value fields so a child Blueprint can override
 * false/zero/null/text values against a genuine, deterministic parent baseline. The tokens are
 * reflected FProperties on a real UObject CDO, so the delta read compares real inherited layout
 * rather than a fabricated memory buffer. Only the dedicated token-read delta packages are
 * created and retired by the delta test; no other test uses this fixture.
 */
UCLASS()
class ACortexBPDeltaParentActor : public AActor
{
	GENERATED_BODY()

public:
	ACortexBPDeltaParentActor();

	// Inherited by the child Blueprint and left untouched, so it stays equal to the parent baseline.
	UPROPERTY(EditAnywhere)
	int32 InheritedEqual = 7;

	// Inherited by the child Blueprint and overridden with an explicit zero.
	UPROPERTY(EditAnywhere)
	int32 InheritedZero = 5;

	// Inherited by the child Blueprint and overridden with a different display text.
	UPROPERTY(EditAnywhere)
	FText InheritedText;

	// Inherited by the child Blueprint and overridden with an explicit null.
	UPROPERTY(EditAnywhere)
	TObjectPtr<UObject> InheritedObject = nullptr;
};
