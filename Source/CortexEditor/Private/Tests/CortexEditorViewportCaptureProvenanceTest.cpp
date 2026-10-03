#include "Misc/AutomationTest.h"
#include "CortexEditorCommandHandler.h"
#include "CortexTypes.h"

#include "Editor.h"
#include "Engine/World.h"
#include "LevelEditor.h"
#include "IAssetViewport.h"
#include "EditorViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
/** Same resolution as production: the level editor's first active viewport. */
TSharedPtr<IAssetViewport> GetActiveAssetViewportForTest()
{
	if (!FSlateApplication::IsInitialized())
	{
		return nullptr;
	}

	FLevelEditorModule* LevelEditorModule =
		FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
	if (LevelEditorModule == nullptr)
	{
		return nullptr;
	}

	return LevelEditorModule->GetFirstActiveViewport();
}

FString GetViewportTestDir()
{
	return FPaths::ProjectSavedDir() / TEXT("CortexTests");
}

FString MakeCapturePath(const FString& LeafName)
{
	const FString Dir = GetViewportTestDir();
	IFileManager::Get().MakeDirectory(*Dir, true);
	return Dir / LeafName;
}

void DeleteCaptureFile(const FString& Path)
{
	if (!Path.IsEmpty())
	{
		IFileManager::Get().Delete(*Path, false, true, true);
	}
}

TSharedPtr<FJsonObject> MakeVectorObject(double X, double Y, double Z)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("x"), X);
	Object->SetNumberField(TEXT("y"), Y);
	Object->SetNumberField(TEXT("z"), Z);
	return Object;
}

TSharedPtr<FJsonObject> MakeRotatorObject(double Pitch, double Yaw, double Roll)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("pitch"), Pitch);
	Object->SetNumberField(TEXT("yaw"), Yaw);
	Object->SetNumberField(TEXT("roll"), Roll);
	return Object;
}

TSharedPtr<FJsonObject> MakeSetCameraParams(
	double X, double Y, double Z, double Pitch, double Yaw, double Roll)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetObjectField(TEXT("location"), MakeVectorObject(X, Y, Z));
	Params->SetObjectField(TEXT("rotation"), MakeRotatorObject(Pitch, Yaw, Roll));
	return Params;
}

TSharedPtr<FJsonObject> MakeCaptureParams(const FString& OutputPath)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("output_path"), OutputPath);
	return Params;
}

/** Snapshot of the active editor viewport pose so a test can restore it afterwards. */
struct FViewportPoseSnapshot
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	bool bValid = false;
};

FViewportPoseSnapshot CaptureActiveViewportPose()
{
	FViewportPoseSnapshot Snapshot;

	TSharedPtr<IAssetViewport> Viewport = GetActiveAssetViewportForTest();
	if (Viewport.IsValid() && Viewport->GetActiveViewport() != nullptr)
	{
		FEditorViewportClient& Client = Viewport->GetAssetViewportClient();
		Snapshot.Location = Client.GetViewLocation();
		Snapshot.Rotation = Client.GetViewRotation();
		Snapshot.bValid = true;
	}

	return Snapshot;
}

TSharedPtr<FJsonObject> MakeSetCameraParamsFromPose(const FViewportPoseSnapshot& Pose)
{
	return MakeSetCameraParams(
		Pose.Location.X, Pose.Location.Y, Pose.Location.Z,
		Pose.Rotation.Pitch, Pose.Rotation.Yaw, Pose.Rotation.Roll);
}

/** Best-effort restore so tests leave the shared editor viewport as they found it. */
void RestoreActiveViewportPose(
	FAutomationTestBase* Test,
	const FViewportPoseSnapshot& Pose,
	const TCHAR* Context)
{
	if (!Pose.bValid)
	{
		return;
	}

	FCortexEditorCommandHandler Handler;
	const FCortexCommandResult Result =
		Handler.Execute(TEXT("set_viewport_camera"), MakeSetCameraParamsFromPose(Pose));
	if (!Result.bSuccess)
	{
		Test->AddInfo(FString::Printf(
			TEXT("%s: could not restore the previous viewport pose (%s)"), Context, *Result.ErrorCode));
	}
}

/** Reads a string field, failing the test when the field is absent (behavioral RED). */
bool RequireString(
	FAutomationTestBase* Test,
	const FCortexCommandResult& Result,
	const TCHAR* Field,
	FString& OutValue,
	const TCHAR* Context)
{
	if (Result.Data.IsValid() && Result.Data->TryGetStringField(Field, OutValue))
	{
		return true;
	}

	Test->AddError(FString::Printf(
		TEXT("%s: response is missing string field '%s'"), Context, Field));
	return false;
}

/** Reads a bool field, failing the test when the field is absent (behavioral RED). */
bool RequireBool(
	FAutomationTestBase* Test,
	const FCortexCommandResult& Result,
	const TCHAR* Field,
	bool& bOutValue,
	const TCHAR* Context)
{
	if (Result.Data.IsValid() && Result.Data->TryGetBoolField(Field, bOutValue))
	{
		return true;
	}

	Test->AddError(FString::Printf(
		TEXT("%s: response is missing bool field '%s'"), Context, Field));
	return false;
}

/** Reads camera.location / camera.rotation from a capture response. */
bool ReadCaptureCameraPose(
	FAutomationTestBase* Test,
	const FCortexCommandResult& Result,
	FVector& OutLocation,
	FRotator& OutRotation,
	const TCHAR* Context)
{
	const TSharedPtr<FJsonObject>* CameraObject = nullptr;
	if (!Result.Data.IsValid()
		|| !Result.Data->TryGetObjectField(TEXT("camera"), CameraObject)
		|| CameraObject == nullptr
		|| !(*CameraObject).IsValid())
	{
		Test->AddError(FString::Printf(
			TEXT("%s: response is missing camera object"), Context));
		return false;
	}

	const TSharedPtr<FJsonObject>* LocationObject = nullptr;
	if (!(*CameraObject)->TryGetObjectField(TEXT("location"), LocationObject)
		|| LocationObject == nullptr)
	{
		Test->AddError(FString::Printf(
			TEXT("%s: camera object is missing location"), Context));
		return false;
	}

	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;
	(*LocationObject)->TryGetNumberField(TEXT("x"), X);
	(*LocationObject)->TryGetNumberField(TEXT("y"), Y);
	(*LocationObject)->TryGetNumberField(TEXT("z"), Z);
	OutLocation = FVector(X, Y, Z);

	const TSharedPtr<FJsonObject>* RotationObject = nullptr;
	if (!(*CameraObject)->TryGetObjectField(TEXT("rotation"), RotationObject)
		|| RotationObject == nullptr)
	{
		Test->AddError(FString::Printf(
			TEXT("%s: camera object is missing rotation"), Context));
		return false;
	}

	double Pitch = 0.0;
	double Yaw = 0.0;
	double Roll = 0.0;
	(*RotationObject)->TryGetNumberField(TEXT("pitch"), Pitch);
	(*RotationObject)->TryGetNumberField(TEXT("yaw"), Yaw);
	(*RotationObject)->TryGetNumberField(TEXT("roll"), Roll);
	OutRotation = FRotator(Pitch, Yaw, Roll);

	return true;
}

/** Provenance must describe the pose of the viewport actually being read. */
void TestCaptureCameraMatchesClient(
	FAutomationTestBase* Test,
	const FEditorViewportClient& Client,
	const FCortexCommandResult& Result,
	const TCHAR* Context)
{
	FVector ResponseLocation;
	FRotator ResponseRotation;
	if (!ReadCaptureCameraPose(Test, Result, ResponseLocation, ResponseRotation, Context))
	{
		return;
	}

	const FVector ClientLocation = Client.GetViewLocation();
	const FRotator ClientRotation = Client.GetViewRotation();

	Test->TestTrue(
		FString::Printf(TEXT("%s: camera.location must equal the observed editor client location"), Context),
		ResponseLocation.Equals(ClientLocation, 0.1));
	Test->TestTrue(
		FString::Printf(TEXT("%s: camera.rotation must equal the observed editor client rotation"), Context),
		ResponseRotation.Equals(ClientRotation, 0.1));
}

// ---------------------------------------------------------------------------
// Latent helpers for PIE scenarios
// ---------------------------------------------------------------------------

class FCortexWaitForViewportPIEPlayingCommand : public IAutomationLatentCommand
{
public:
	explicit FCortexWaitForViewportPIEPlayingCommand(FAutomationTestBase* InTest)
		: Test(InTest)
		, StartTime(FPlatformTime::Seconds())
	{
	}

	virtual bool Update() override
	{
		if (!GEditor)
		{
			Test->AddError(TEXT("GEditor is null while waiting for PIE to start"));
			return true;
		}

		if (GEditor->PlayWorld != nullptr
			&& GEditor->PlayWorld->HasBegunPlay()
			&& GEditor->PlayWorld->GetFirstPlayerController() != nullptr)
		{
			return true;
		}

		if ((FPlatformTime::Seconds() - StartTime) > 20.0)
		{
			Test->AddError(TEXT("Timed out waiting for PIE to begin play"));
			return true;
		}

		return false;
	}

private:
	FAutomationTestBase* Test;
	double StartTime;
};

class FCortexWaitForViewportPIEStoppedCommand : public IAutomationLatentCommand
{
public:
	explicit FCortexWaitForViewportPIEStoppedCommand(FAutomationTestBase* InTest)
		: Test(InTest)
		, StartTime(FPlatformTime::Seconds())
	{
	}

	virtual bool Update() override
	{
		if (!GEditor)
		{
			Test->AddError(TEXT("GEditor is null while waiting for PIE to stop"));
			return true;
		}

		if (GEditor->PlayWorld == nullptr && !GEditor->bIsSimulatingInEditor)
		{
			return true;
		}

		if ((FPlatformTime::Seconds() - StartTime) > 20.0)
		{
			Test->AddError(TEXT("Timed out waiting for PIE to stop"));
			return true;
		}

		return false;
	}

private:
	FAutomationTestBase* Test;
	double StartTime;
};

/** Requests the same engine path the editor's Eject button uses (PIE -> SIE). */
class FCortexRequestEjectToSIECommand : public IAutomationLatentCommand
{
public:
	explicit FCortexRequestEjectToSIECommand(FAutomationTestBase* InTest)
		: Test(InTest)
	{
	}

	virtual bool Update() override
	{
		if (!GEditor)
		{
			Test->AddError(TEXT("GEditor is null while requesting PIE eject"));
			return true;
		}

		GEditor->RequestToggleBetweenPIEandSIE();
		return true;
	}

private:
	FAutomationTestBase* Test;
};

class FCortexWaitForViewportSIECommand : public IAutomationLatentCommand
{
public:
	explicit FCortexWaitForViewportSIECommand(FAutomationTestBase* InTest)
		: Test(InTest)
		, StartTime(FPlatformTime::Seconds())
	{
	}

	virtual bool Update() override
	{
		if (!GEditor)
		{
			Test->AddError(TEXT("GEditor is null while waiting for SIE"));
			return true;
		}

		if (GEditor->bIsSimulatingInEditor)
		{
			return true;
		}

		if ((FPlatformTime::Seconds() - StartTime) > 15.0)
		{
			Test->AddError(TEXT("Timed out waiting for the PIE session to eject into SIE"));
			return true;
		}

		return false;
	}

private:
	FAutomationTestBase* Test;
	double StartTime;
};

/** Restores the pre-test editor viewport pose after a PIE scenario tears down. */
class FCortexRestoreViewportPoseCommand : public IAutomationLatentCommand
{
public:
	FCortexRestoreViewportPoseCommand(FAutomationTestBase* InTest, FViewportPoseSnapshot InPose)
		: Test(InTest)
		, Pose(InPose)
	{
	}

	virtual bool Update() override
	{
		RestoreActiveViewportPose(Test, Pose, TEXT("viewport teardown restore"));
		return true;
	}

private:
	FAutomationTestBase* Test;
	FViewportPoseSnapshot Pose;
};
}

// ---------------------------------------------------------------------------
// 1. Editor-camera capture provenance without PIE
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorViewportEditorCameraProvenanceTest,
	"Cortex.Editor.Viewport.Capture.EditorCameraProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorViewportEditorCameraProvenanceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	if (GEditor != nullptr && GEditor->PlayWorld != nullptr)
	{
		AddError(TEXT("PIE must not be running for the editor-camera provenance case"));
		return false;
	}

	TSharedPtr<IAssetViewport> Viewport = GetActiveAssetViewportForTest();
	if (!Viewport.IsValid() || Viewport->GetActiveViewport() == nullptr)
	{
		AddError(TEXT("No active editor viewport available; cannot verify capture provenance"));
		return false;
	}

	FCortexEditorCommandHandler Handler;

	// Remember the original pose so this test leaves the shared viewport as it found it.
	const FViewportPoseSnapshot PreTestPose = CaptureActiveViewportPose();

	// Move to a distinctive pose so the response cannot match an incidental default.
	const FVector TargetLocation(1234.5, -678.25, 910.75);
	const FRotator TargetRotation(12.5, 34.75, 0.0);

	const FCortexCommandResult SetResult = Handler.Execute(
		TEXT("set_viewport_camera"),
		MakeSetCameraParams(TargetLocation.X, TargetLocation.Y, TargetLocation.Z,
			TargetRotation.Pitch, TargetRotation.Yaw, TargetRotation.Roll));

	TestTrue(TEXT("set_viewport_camera should succeed outside PIE"), SetResult.bSuccess);
	if (SetResult.Data.IsValid())
	{
		FString Status;
		if (RequireString(this, SetResult, TEXT("status"), Status, TEXT("editor set_viewport_camera")))
		{
			TestEqual(TEXT("editor set_viewport_camera status should be ok"), Status, FString(TEXT("ok")));
		}

		bool bPieActive = true;
		if (RequireBool(this, SetResult, TEXT("pie_active"), bPieActive, TEXT("editor set_viewport_camera")))
		{
			TestFalse(TEXT("editor set_viewport_camera should report pie_active=false"), bPieActive);
		}

		bool bEditorCameraVisible = false;
		if (RequireBool(this, SetResult, TEXT("editor_camera_visible"), bEditorCameraVisible, TEXT("editor set_viewport_camera")))
		{
			TestTrue(TEXT("editor set_viewport_camera should report editor_camera_visible=true"), bEditorCameraVisible);
		}
	}

	FEditorViewportClient& Client = Viewport->GetAssetViewportClient();
	TestTrue(
		TEXT("active editor client should adopt the requested location"),
		Client.GetViewLocation().Equals(TargetLocation, 0.1));
	TestTrue(
		TEXT("active editor client should adopt the requested rotation"),
		Client.GetViewRotation().Equals(TargetRotation, 0.1));

	const FString CapturePath = MakeCapturePath(TEXT("cortex_viewport_editor_provenance.png"));
	const FCortexCommandResult CaptureResult =
		Handler.Execute(TEXT("capture_screenshot"), MakeCaptureParams(CapturePath));

	TestTrue(TEXT("capture_screenshot should succeed outside PIE"), CaptureResult.bSuccess);
	if (CaptureResult.bSuccess)
	{
		FString View;
		if (RequireString(this, CaptureResult, TEXT("view"), View, TEXT("editor capture")))
		{
			TestEqual(TEXT("editor capture view should be editor_camera"), View, FString(TEXT("editor_camera")));
		}

		bool bPieActive = true;
		if (RequireBool(this, CaptureResult, TEXT("pie_active"), bPieActive, TEXT("editor capture")))
		{
			TestFalse(TEXT("editor capture should report pie_active=false"), bPieActive);
		}

		bool bCameraAvailable = false;
		if (RequireBool(this, CaptureResult, TEXT("camera_available"), bCameraAvailable, TEXT("editor capture")))
		{
			TestTrue(TEXT("editor capture should report camera_available=true"), bCameraAvailable);
		}

		FString Provenance;
		if (RequireString(this, CaptureResult, TEXT("camera_provenance"), Provenance, TEXT("editor capture")))
		{
			TestEqual(TEXT("editor capture provenance should be editor_client"), Provenance, FString(TEXT("editor_client")));
		}

		TestCaptureCameraMatchesClient(this, Client, CaptureResult, TEXT("editor capture"));
	}

	DeleteCaptureFile(CapturePath);
	RestoreActiveViewportPose(this, PreTestPose, TEXT("editor provenance teardown"));
	return true;
}

// ---------------------------------------------------------------------------
// 2. Possessed PIE: honest game-camera provenance and a non-mutating default guard
// ---------------------------------------------------------------------------

namespace
{
class FCortexViewportPossessedPIEChecksCommand : public IAutomationLatentCommand
{
public:
	explicit FCortexViewportPossessedPIEChecksCommand(FAutomationTestBase* InTest)
		: Test(InTest)
	{
	}

	virtual bool Update() override
	{
		if (!GEditor || GEditor->PlayWorld == nullptr)
		{
			Test->AddError(TEXT("Expected an active PIE session for the possessed-viewport checks"));
			return true;
		}

		Test->TestNotNull(TEXT("GEditor->PlayWorld should be set while PIE is active"), GEditor->PlayWorld.Get());

		TSharedPtr<IAssetViewport> Viewport = GetActiveAssetViewportForTest();
		if (!Viewport.IsValid() || Viewport->GetActiveViewport() == nullptr)
		{
			Test->AddError(TEXT("No active viewport during possessed PIE"));
			return true;
		}

		Test->TestTrue(
			TEXT("the active viewport should hold a PIE viewport during in-viewport play"),
			Viewport->HasPlayInEditorViewport());

		FEditorViewportClient& Client = Viewport->GetAssetViewportClient();
		FCortexEditorCommandHandler Handler;

		// The default guard must reject a possessed PIE viewport and leave the client pose untouched.
		const FVector PoseBefore = Client.GetViewLocation();
		const FRotator RotationBefore = Client.GetViewRotation();

		const FCortexCommandResult GuardResult = Handler.Execute(
			TEXT("set_viewport_camera"), MakeSetCameraParams(1111.0, 2222.0, 3333.0, 5.0, 6.0, 0.0));

		Test->TestFalse(
			TEXT("set_viewport_camera must reject a possessed PIE viewport without allow_during_pie"),
			GuardResult.bSuccess);
		Test->TestEqual(
			TEXT("rejected possessed PIE should report INVALID_OPERATION"),
			GuardResult.ErrorCode, FString(TEXT("INVALID_OPERATION")));
		Test->TestTrue(
			TEXT("rejected guard must not move the editor client"),
			Client.GetViewLocation().Equals(PoseBefore, 0.01));
		Test->TestTrue(
			TEXT("rejected guard must not rotate the editor client"),
			Client.GetViewRotation().Equals(RotationBefore, 0.01));

		// The capture must classify the active viewport as the game camera and refuse to
		// fabricate a pose from the first player controller.
		const FString CapturePath = MakeCapturePath(TEXT("cortex_viewport_possessed_pie.png"));
		const FCortexCommandResult CaptureResult =
			Handler.Execute(TEXT("capture_screenshot"), MakeCaptureParams(CapturePath));
		Test->TestTrue(TEXT("capture_screenshot should succeed during PIE"), CaptureResult.bSuccess);

		if (CaptureResult.bSuccess)
		{
			FString View;
			if (RequireString(Test, CaptureResult, TEXT("view"), View, TEXT("possessed PIE capture")))
			{
				Test->TestEqual(
					TEXT("possessed PIE capture view should be pie_game_camera"),
					View, FString(TEXT("pie_game_camera")));
			}

			bool bPieActive = false;
			if (RequireBool(Test, CaptureResult, TEXT("pie_active"), bPieActive, TEXT("possessed PIE capture")))
			{
				Test->TestTrue(TEXT("possessed PIE capture should report pie_active=true"), bPieActive);
			}

			bool bCameraAvailable = true;
			if (RequireBool(Test, CaptureResult, TEXT("camera_available"), bCameraAvailable, TEXT("possessed PIE capture")))
			{
				Test->TestFalse(
					TEXT("game-camera capture must not claim camera_available=true"),
					bCameraAvailable);
			}

			FString Provenance;
			if (RequireString(Test, CaptureResult, TEXT("camera_provenance"), Provenance, TEXT("possessed PIE capture")))
			{
				Test->TestEqual(
					TEXT("game-camera capture provenance should be unavailable"),
					Provenance, FString(TEXT("unavailable")));
			}

			const TSharedPtr<FJsonObject>* CameraObject = nullptr;
			if (CaptureResult.Data->TryGetObjectField(TEXT("camera"), CameraObject)
				&& CameraObject != nullptr && (*CameraObject).IsValid())
			{
				Test->TestFalse(
					TEXT("unavailable game-camera capture must not include camera.location"),
					(*CameraObject)->HasField(TEXT("location")));
			}
		}
		DeleteCaptureFile(CapturePath);

		// The explicit transient override must be honored and must actually move the editor client.
		TSharedPtr<FJsonObject> OverrideParams =
			MakeSetCameraParams(4444.0, 5555.0, 6666.0, 10.0, 20.0, 0.0);
		OverrideParams->SetBoolField(TEXT("allow_during_pie"), true);

		const FCortexCommandResult OverrideResult =
			Handler.Execute(TEXT("set_viewport_camera"), OverrideParams);
		Test->TestTrue(TEXT("allow_during_pie=true should permit the transient override"), OverrideResult.bSuccess);

		if (OverrideResult.Data.IsValid())
		{
			FString Status;
			if (RequireString(Test, OverrideResult, TEXT("status"), Status, TEXT("possessed PIE override")))
			{
				Test->TestEqual(TEXT("override status should be ok"), Status, FString(TEXT("ok")));
			}

			bool bPieActive = false;
			if (RequireBool(Test, OverrideResult, TEXT("pie_active"), bPieActive, TEXT("possessed PIE override")))
			{
				Test->TestTrue(TEXT("override should report pie_active=true"), bPieActive);
			}

			bool bEditorCameraVisible = true;
			if (RequireBool(Test, OverrideResult, TEXT("editor_camera_visible"), bEditorCameraVisible, TEXT("possessed PIE override")))
			{
				Test->TestFalse(
					TEXT("possessed PIE override should report editor_camera_visible=false"),
					bEditorCameraVisible);
			}

			FString Note;
			if (RequireString(Test, OverrideResult, TEXT("note"), Note, TEXT("possessed PIE override")))
			{
				Test->TestFalse(TEXT("override note should explain the transient client change"), Note.IsEmpty());
			}
		}

		Test->TestTrue(
			TEXT("allow_during_pie override must actually reposition the editor client"),
			Client.GetViewLocation().Equals(FVector(4444.0, 5555.0, 6666.0), 0.1));

		return true;
	}

private:
	FAutomationTestBase* Test;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorViewportPossessedPIEProvenanceTest,
	"Cortex.Editor.Viewport.SetCamera.PossessedPIEGuardAndOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorViewportPossessedPIEProvenanceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FViewportPoseSnapshot PreTestPose = CaptureActiveViewportPose();

	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCortexWaitForViewportPIEPlayingCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FCortexViewportPossessedPIEChecksCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FCortexWaitForViewportPIEStoppedCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FCortexRestoreViewportPoseCommand(this, PreTestPose));

	return true;
}

// ---------------------------------------------------------------------------
// 3. Ejected/SIE: the active editor viewport stays classified and controllable
// ---------------------------------------------------------------------------

namespace
{
class FCortexViewportEjectedSIEChecksCommand : public IAutomationLatentCommand
{
public:
	explicit FCortexViewportEjectedSIEChecksCommand(FAutomationTestBase* InTest)
		: Test(InTest)
	{
	}

	virtual bool Update() override
	{
		if (!GEditor || GEditor->PlayWorld == nullptr)
		{
			Test->AddError(TEXT("Expected an active play session after ejecting into SIE"));
			return true;
		}

		Test->TestTrue(TEXT("ejected PIE session should still be simulating in editor"), GEditor->bIsSimulatingInEditor);

		TSharedPtr<IAssetViewport> Viewport = GetActiveAssetViewportForTest();
		if (!Viewport.IsValid() || Viewport->GetActiveViewport() == nullptr)
		{
			Test->AddError(TEXT("No active viewport after ejecting into SIE"));
			return true;
		}

		Test->AddInfo(FString::Printf(
			TEXT("ejected viewport HasPlayInEditorViewport=%s (expected true: the inactive PIE viewport still exists)"),
			Viewport->HasPlayInEditorViewport() ? TEXT("true") : TEXT("false")));

		FEditorViewportClient& Client = Viewport->GetAssetViewportClient();
		FCortexEditorCommandHandler Handler;

		// The active viewport now renders the editor camera, so the capture must say so even
		// though the PIE session is still running.
		const FString CapturePath = MakeCapturePath(TEXT("cortex_viewport_ejected_sie.png"));
		const FCortexCommandResult CaptureResult =
			Handler.Execute(TEXT("capture_screenshot"), MakeCaptureParams(CapturePath));
		Test->TestTrue(TEXT("capture_screenshot should succeed after ejecting into SIE"), CaptureResult.bSuccess);

		if (CaptureResult.bSuccess)
		{
			FString View;
			if (RequireString(Test, CaptureResult, TEXT("view"), View, TEXT("ejected capture")))
			{
				Test->TestEqual(
					TEXT("ejected active viewport must classify as editor_camera, not the inactive PIE viewport"),
					View, FString(TEXT("editor_camera")));
			}

			bool bPieActive = false;
			if (RequireBool(Test, CaptureResult, TEXT("pie_active"), bPieActive, TEXT("ejected capture")))
			{
				Test->TestTrue(
					TEXT("ejected capture should still report pie_active=true (session presence, not camera source)"),
					bPieActive);
			}

			bool bCameraAvailable = false;
			if (RequireBool(Test, CaptureResult, TEXT("camera_available"), bCameraAvailable, TEXT("ejected capture")))
			{
				Test->TestTrue(TEXT("ejected editor viewport capture should report camera_available=true"), bCameraAvailable);
			}

			FString Provenance;
			if (RequireString(Test, CaptureResult, TEXT("camera_provenance"), Provenance, TEXT("ejected capture")))
			{
				Test->TestEqual(
					TEXT("ejected capture provenance should be editor_client"),
					Provenance, FString(TEXT("editor_client")));
			}

			TestCaptureCameraMatchesClient(Test, Client, CaptureResult, TEXT("ejected capture"));
		}
		DeleteCaptureFile(CapturePath);

		// An ejected/SIE editor viewport is genuinely controllable: no override flag required.
		const FCortexCommandResult SetResult = Handler.Execute(
			TEXT("set_viewport_camera"), MakeSetCameraParams(777.0, 888.0, 999.0, 15.0, 25.0, 0.0));
		Test->TestTrue(
			TEXT("set_viewport_camera must allow a controllable ejected/SIE editor viewport without allow_during_pie"),
			SetResult.bSuccess);

		if (SetResult.Data.IsValid())
		{
			FString Status;
			if (RequireString(Test, SetResult, TEXT("status"), Status, TEXT("ejected set_viewport_camera")))
			{
				Test->TestEqual(TEXT("ejected set_viewport_camera status should be ok"), Status, FString(TEXT("ok")));
			}

			bool bPieActive = false;
			if (RequireBool(Test, SetResult, TEXT("pie_active"), bPieActive, TEXT("ejected set_viewport_camera")))
			{
				Test->TestTrue(TEXT("ejected set_viewport_camera should report pie_active=true"), bPieActive);
			}

			bool bEditorCameraVisible = false;
			if (RequireBool(Test, SetResult, TEXT("editor_camera_visible"), bEditorCameraVisible, TEXT("ejected set_viewport_camera")))
			{
				Test->TestTrue(
					TEXT("ejected set_viewport_camera should report editor_camera_visible=true"),
					bEditorCameraVisible);
			}
		}

		Test->TestTrue(
			TEXT("ejected set_viewport_camera must actually move the editor client"),
			Client.GetViewLocation().Equals(FVector(777.0, 888.0, 999.0), 0.1));

		return true;
	}

private:
	FAutomationTestBase* Test;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorViewportEjectedSIEProvenanceTest,
	"Cortex.Editor.Viewport.Capture.EjectedSIEProvenanceAndControl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorViewportEjectedSIEProvenanceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FViewportPoseSnapshot PreTestPose = CaptureActiveViewportPose();

	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCortexWaitForViewportPIEPlayingCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FCortexRequestEjectToSIECommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FCortexWaitForViewportSIECommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FCortexViewportEjectedSIEChecksCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FCortexWaitForViewportPIEStoppedCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FCortexRestoreViewportPoseCommand(this, PreTestPose));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorViewportStrictOverrideTypeTest,
	"Cortex.Editor.Viewport.SetCamera.MalformedOverridePreservesPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorViewportStrictOverrideTypeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FViewportPoseSnapshot Pose = CaptureActiveViewportPose();
	TSharedPtr<IAssetViewport> Viewport = GetActiveAssetViewportForTest();
	if (!Pose.bValid || !Viewport.IsValid())
	{
		AddError(TEXT("An active viewport is required to observe rejected override mutation"));
		return false;
	}
	FCortexEditorCommandHandler Handler;
	const TArray<TSharedPtr<FJsonValue>> InvalidOverrides = {
		MakeShared<FJsonValueNumber>(1.0),
		MakeShared<FJsonValueString>(TEXT("true"))
	};
	for (const TSharedPtr<FJsonValue>& Override : InvalidOverrides)
	{
		TSharedPtr<FJsonObject> Params = MakeSetCameraParams(
			Pose.Location.X + 500.0, Pose.Location.Y + 300.0, Pose.Location.Z + 200.0,
			15.0, 35.0, 0.0);
		Params->SetField(TEXT("allow_during_pie"), Override);
		const FCortexCommandResult Result = Handler.Execute(TEXT("set_viewport_camera"), Params);
		TestFalse(TEXT("boolean-convertible number/string cannot authorize camera mutation"), Result.bSuccess);
		TestEqual(TEXT("non-boolean override is INVALID_FIELD"), Result.ErrorCode, FString(TEXT("INVALID_FIELD")));
		FEditorViewportClient& Client = Viewport->GetAssetViewportClient();
		TestTrue(TEXT("invalid override preserves actual client location"), Client.GetViewLocation().Equals(Pose.Location, 0.1));
		TestTrue(TEXT("invalid override preserves actual client rotation"), Client.GetViewRotation().Equals(Pose.Rotation, 0.1));
		RestoreActiveViewportPose(this, Pose, TEXT("override type regression"));
	}
	return true;
}
