#include "CortexVersionCompat.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "WidgetBlueprintEditorUtils.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetNavigation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Editor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_Variable.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UnrealType.h"

#if (ENGINE_MAJOR_VERSION > 5 || ENGINE_MINOR_VERSION >= 6) && __has_include("WidgetBlueprintOperationUtils.h")
#include "WidgetBlueprintOperationUtils.h"
#define CORTEX_NATIVE_WIDGET_RENAME 1
#else
#define CORTEX_NATIVE_WIDGET_RENAME 0
#endif

namespace
{
#if !CORTEX_NATIVE_WIDGET_RENAME
FWidgetBlueprintEditor* FindOpenWidgetEditor(UWidgetBlueprint* Blueprint)
{
    UAssetEditorSubsystem* Editors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
    return Editors ? static_cast<FWidgetBlueprintEditor*>(Editors->FindEditorForAsset(Blueprint, false)) : nullptr;
}

bool IsCompatibleBindWidget(UWidgetBlueprint* Blueprint, UWidget* Widget, FName Name)
{
    const FObjectPropertyBase* Property = Blueprint->ParentClass
        ? CastField<FObjectPropertyBase>(Blueprint->ParentClass->FindPropertyByName(Name)) : nullptr;
    return Property && FWidgetBlueprintEditorUtils::IsBindWidgetProperty(Property)
        && Widget->IsA(Property->PropertyClass);
}
#endif
}

template <>
bool CortexVerifyWidgetRename(UWidgetBlueprint* Blueprint, UWidget* Widget, const FText& Name, FText& Error)
{
#if CORTEX_NATIVE_WIDGET_RENAME
    return FWidgetBlueprintOperationUtils::VerifyWidgetRename(Blueprint, Widget, Name, Error);
#else
    const FString NewName = Name.ToString();
    if (!Blueprint || !Blueprint->WidgetTree || !Widget || Name.IsEmptyOrWhitespace()
        || NewName.Len() >= NAME_SIZE || !FName(*NewName).IsValidXName(INVALID_OBJECTNAME_CHARACTERS))
    {
        Error = FText::FromString(TEXT("Invalid widget name"));
        return false;
    }
    const FName NewFName(*NewName);
    if (UWidget* Existing = Blueprint->WidgetTree->FindWidget(NewFName))
    {
        if (Existing != Widget)
        {
            Error = FText::FromString(TEXT("Existing widget name"));
            return false;
        }
    }
    if (!Widget->Rename(*NewName, nullptr, REN_Test))
    {
        Error = FText::FromString(TEXT("Existing object name"));
        return false;
    }
    if (FWidgetBlueprintEditor* Editor = FindOpenWidgetEditor(Blueprint))
    {
        if (UWidget* Preview = Editor->GetReferenceFromTemplate(Widget).GetPreview())
        {
            if (!Preview->Rename(*NewName, nullptr, REN_Test))
            {
                Error = FText::FromString(TEXT("Existing preview object name"));
                return false;
            }
        }
    }
    if (IsCompatibleBindWidget(Blueprint, Widget, NewFName)) { return true; }
    const FObjectPropertyBase* ParentProperty = Blueprint->ParentClass
        ? CastField<FObjectPropertyBase>(Blueprint->ParentClass->FindPropertyByName(NewFName)) : nullptr;
    if (ParentProperty && FWidgetBlueprintEditorUtils::IsBindWidgetProperty(ParentProperty))
    {
        Error = FText::FromString(TEXT("Widget binding has an incompatible widget class"));
        return false;
    }
    FKismetNameValidator Validator(Blueprint);
    const EValidatorResult Result = Validator.IsValid(NewFName);
    if (Result != EValidatorResult::Ok)
    {
        Error = INameValidatorInterface::GetErrorText(NewName, Result);
        return false;
    }
    return true;
#endif
}

template <>
bool CortexRenameWidget(UWidgetBlueprint* Blueprint, UWidget* Widget, const FString& Name)
{
#if CORTEX_NATIVE_WIDGET_RENAME
    return FWidgetBlueprintOperationUtils::RenameWidget(Blueprint, Widget, Name);
#else
    FText Error;
    if (!CortexVerifyWidgetRename(Blueprint, Widget, FText::FromString(Name), Error)) { return false; }
    const FName OldName = Widget->GetFName();
    const FName NewName(*Name);
    Blueprint->Modify();
    Widget->Modify();
    auto& Guids = CortexWidgetVariableGuids(*Blueprint);
    const FGuid Guid = Guids.FindRef(OldName);
    // The caller owns the transaction and has already captured animation state.
    if (FWidgetBlueprintEditor* Editor = FindOpenWidgetEditor(Blueprint))
    {
        if (UWidget* Preview = Editor->GetReferenceFromTemplate(Widget).GetPreview())
        {
            if (Preview != Widget)
            {
                Preview->Modify();
                Preview->SetDisplayLabel(Name);
                Preview->Rename(*Name);
            }
        }
    }
    Widget->SetDisplayLabel(Name);
    if (!Widget->Rename(*Name)) { return false; }

    Guids.Remove(OldName);
    Guids.Add(NewName, Guid.IsValid() ? Guid : FGuid::NewGuid());
#if UE_HAS_WIDGET_GENERATED_BY_CLASS
    // Match the native Designer's repair of pre-existing getters for this name.
    if (Widget->bIsVariable)
    {
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (const UEdGraph* Graph : Graphs)
        {
            TArray<UK2Node_Variable*> Variables;
            Graph->GetNodesOfClass(Variables);
            for (UK2Node_Variable* Variable : Variables)
            {
                UClass* SelfClass = Blueprint->GeneratedClass;
                if (Variable->VariableReference.GetMemberParentClass(SelfClass) == SelfClass
                    && Variable->GetVarName() == NewName)
                {
                    if (UEdGraphPin* ValuePin = Variable->GetValuePin())
                    {
                        ValuePin->Modify();
                        Variable->Modify();
                        Variable->CreatePin(ValuePin->Direction, ValuePin->PinType.PinCategory,
                            ValuePin->PinType.PinSubCategory, Widget->WidgetGeneratedByClass.Get(), NewName);
                        ValuePin->bOrphanedPin = true;
                    }
                }
            }
        }
    }
#endif
    for (FDelegateEditorBinding& Binding : Blueprint->Bindings)
    {
        if (Binding.ObjectName == OldName.ToString()) { Binding.ObjectName = Name; }
    }
    auto UpdateFocus = [OldName, NewName](UUserWidget* UserWidget)
    {
        if (UserWidget && UserWidget->GetDesiredFocusWidgetName() == OldName)
        {
            UserWidget->SetFlags(RF_Transactional);
            UserWidget->Modify();
            UserWidget->SetDesiredFocusWidget(NewName);
        }
    };
    if (Blueprint->GeneratedClass) { UpdateFocus(Blueprint->GeneratedClass->GetDefaultObject<UUserWidget>()); }
    if (FWidgetBlueprintEditor* Editor = FindOpenWidgetEditor(Blueprint)) { UpdateFocus(Editor->GetPreview()); }
    for (UWidgetAnimation* Animation : Blueprint->Animations)
    {
        if (!Animation) { continue; }
        for (FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
        {
            if (Binding.WidgetName != OldName) { continue; }
            Animation->Modify();
            Binding.WidgetName = NewName;
            if (Animation->MovieScene)
            {
                Animation->MovieScene->Modify();
                if (Binding.SlotWidgetName == NAME_None)
                {
                    if (FMovieScenePossessable* Possessable = Animation->MovieScene->FindPossessable(Binding.AnimationGuid))
                    {
                        Possessable->SetName(Name);
                    }
                }
            }
        }
    }
    Blueprint->WidgetTree->ForEachWidget([OldName, NewName](UWidget* Child)
    {
        if (Child->Navigation)
        {
            Child->Navigation->SetFlags(RF_Transactional);
            Child->Navigation->Modify();
            Child->Navigation->TryToRenameBinding(OldName, NewName);
        }
    });
    FBlueprintEditorUtils::ValidateBlueprintChildVariables(Blueprint, NewName);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FBlueprintEditorUtils::ReplaceVariableReferences(Blueprint, OldName, NewName);
    return true;
#endif
}
