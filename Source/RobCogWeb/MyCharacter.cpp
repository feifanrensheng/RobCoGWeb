// Fill out your copyright notice in the Description page of Project Settings.

#include "RobCogWeb.h"
#include "MyCharacter.h"
#include "GameFramework/InputSettings.h"


// Sets default values
AMyCharacter::AMyCharacter()
{
	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Set this pawn to be controlled by the lowest-numbered player
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(5.f, 80.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Create a CameraComponent	
	MyCharacterCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("MyCharacterCamera"));
	MyCharacterCamera->SetupAttachment(GetCapsuleComponent());
	MyCharacterCamera->RelativeLocation = FVector(0.f, 0.f, 64.f); // Position the camera
	MyCharacterCamera->bUsePawnControlRotation = true;

	//Set the value of the applied force
	AppliedForce = FVector(1000, 1000, 1000);

	//Initialize TraceParams parameter
	TraceParams = FCollisionQueryParams(FName(TEXT("Trace")), true, this);
	TraceParams.bTraceComplex = true;
	TraceParams.bTraceAsyncScene = true;
	TraceParams.bReturnPhysicalMaterial = false;

	//Set the maximum grasping length
	MaxGraspLength = 200.f;

	//Set the pointers to the items held in hands to null at the begining of the game
	LeftHandSlot = nullptr;
	RightHandSlot = nullptr;

	//Set the rotations to null
	LeftHandRotator = GetActorRotation();
	RightHandRotator = GetActorRotation();

	//By default our character will perfom actions with the right hand first
	bRightHandSelected = true;

	//At the begining we don't hold an item which can be rotated
	bRotationModeAllowed = false;

	// 1 - Z axis ; 2 - X axis ; 3 - Y axis ; 0 - default rotation disabled
	RotationAxisIndex = 0;

	//Offsets for positioning the items held in hand
	RightZPos = 30.f;
	LeftZPos = 30.f;
	RightYPos = 20;
	LeftYPos = 20;

	//Default value for how many items can our character pick at once
	StackGrabLimit = 4;

	//PopUp text messages
	OneHandOcupied = FString(TEXT("Can't pick up with two hands\nif they are not both empty!"));
	SmthOnTop = FString(TEXT("Make sure it does not have anything\non top before picking!"));
	ActionNotValid = FString(TEXT("Action not valid!"));
	GetCloser = FString(TEXT("You need to get closer!"));
}

// Called when the game starts or when spawned
void AMyCharacter::BeginPlay()
{
	Super::BeginPlay();

	//Gets all actors in the world, used for identifying our drawers and setting their initial state to closed
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), AllActors);

	FString LevelName = UGameplayStatics::GetCurrentLevelName(GetWorld());

	if (LevelName == TEXT("TutorialLevel"))
	{
		CurrentLevelInteger = 1;
	}
	else if (LevelName == TEXT("BreakfastLevel"))
	{
		CurrentLevelInteger = 2;
	}
	else if (LevelName == TEXT("CleaningLevel"))
	{
		CurrentLevelInteger = 3;
	}

	//Loop that checks if current object matches our drawer
	for (const auto ActorIt : AllActors)
	{
		//Set default stencil value (for blue outline effect)
		if (GetStaticMesh(ActorIt))
		{
			GetStaticMesh(ActorIt)->SetCustomDepthStencilValue(1);
		}

		//Finds the actors for the Handles, used to set the initial state of our drawers to closed 
		if (ActorIt->GetName().Contains("Handle"))
		{
			if (GetStaticMesh(ActorIt) != nullptr)
			{
				if (!ActorIt->GetName().Contains("Door"))
				{
					GetStaticMesh(ActorIt)->AddImpulse(-1 * AppliedForce * ActorIt->GetActorForwardVector());
				}
				AssetStateMap.Add(ActorIt->GetAttachParentActor(), EAssetState::Closed);
			}
		}
		//Remember to tag items when adding them into the world
		else if (ActorIt->ActorHasTag(FName(TEXT("Item"))))
		{
			ItemMap.Add(ActorIt, EItemType::GeneralItem);
		}

		//Populate the list of stackable items in world
		if (ActorIt->ActorHasTag(FName(TEXT("Stackable"))))
		{
			AllStackableItems.Add(ActorIt);
		}
	}
}

UStaticMeshComponent* AMyCharacter::GetStaticMesh(const AActor* Actor)
{
	for (auto Component : Actor->GetComponents())
	{
		if (Component->GetName().Contains("StaticMeshComponent"))
		{
			return Cast<UStaticMeshComponent>(Component);
		}
	}
	return nullptr;
}

TSet<AActor*> AMyCharacter::GetStack(AActor* ContainedItem)
{
	TSet<AActor*> StackList;
	StackList.Empty();

	if (!ContainedItem->ActorHasTag(FName(TEXT("Stackable"))))
	{
		return StackList;
	}

	for (const auto Iterator : AllStackableItems)
	{
		if (Iterator->Tags == ContainedItem->Tags)
		{
			if ((ContainedItem->GetActorLocation().X - 2 < Iterator->GetActorLocation().X) &&
				(Iterator->GetActorLocation().X < ContainedItem->GetActorLocation().X + 2) &&
				(ContainedItem->GetActorLocation().Y - 2 < Iterator->GetActorLocation().Y) &&
				(Iterator->GetActorLocation().Y< ContainedItem->GetActorLocation().Y + 2))
			{
				StackList.Add(Iterator);
			}
		}
	}
	bool swapped = true;
	int j = 0;
	AActor* tmp;
	while (swapped) {
		swapped = false;
		j++;
		for (int i = 0; i < StackList.Num() - j; i++) {
			if (StackList[FSetElementId::FromInteger(i)]->GetActorLocation().Z > StackList[FSetElementId::FromInteger(i + 1)]->GetActorLocation().Z)
			{
				tmp = StackList[FSetElementId::FromInteger(i)];
				StackList[FSetElementId::FromInteger(i)] = StackList[FSetElementId::FromInteger(i + 1)];
				StackList[FSetElementId::FromInteger(i + 1)] = tmp;
				swapped = true;
			}
		}
	}
	return StackList;
}

void AMyCharacter::GrabWithTwoHands()
{
	//Exit function call if invalid apelation
	if (!HitObject.IsValidBlockingHit() || HitObject.Distance > MaxGraspLength || !HighlightedActor)
	{
		PopUpMessage = ActionNotValid;
		return;                                                                                                             //---> display message
	}

	if (TwoHandSlot.Num() || !HitObject.GetActor()->ActorHasTag(FName(TEXT("Item"))))
	{
		return;
	}

	if (RightHandSlot || LeftHandSlot)
	{
		//UE_LOG(LogTemp, Warning, TEXT("Can't pick up with both hands if they are not both empty!"));                          //---> display message
		PopUpMessage = OneHandOcupied;
		return;
	}


	TSet<AActor*> LocalStackVariable = GetStack(HighlightedActor);
	TSet<AActor*> ReturnStack;
	if (HasAnyOnTop(LocalStackVariable[FSetElementId::FromInteger(LocalStackVariable.Num() - 1)]))
	{
		//UE_LOG(LogTemp, Warning, TEXT("You can't do that because it has something on it!"));                                  //---> display message
		PopUpMessage = SmthOnTop;
		return;
	}
	if (HighlightedActor->ActorHasTag(FName(TEXT("Stackable"))))
	{
		int FirstIndex = 0;
		if (LocalStackVariable.Num() > StackGrabLimit)
		{
			FirstIndex = LocalStackVariable.Num() - StackGrabLimit;
		}

		for (int i = FirstIndex; i < LocalStackVariable.Num(); i++)
		{
			GetStaticMesh(LocalStackVariable[FSetElementId::FromInteger(i)])->SetEnableGravity(false);
			GetStaticMesh(LocalStackVariable[FSetElementId::FromInteger(i)])->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ReturnStack.Add(LocalStackVariable[FSetElementId::FromInteger(i)]);
		}
		TwoHandSlot = ReturnStack;
		SelectedObject = LocalStackVariable[FSetElementId::FromInteger(LocalStackVariable.Num()-1)];
		GetStaticMesh(SelectedObject)->SetCustomDepthStencilValue(2);
	}
}

void AMyCharacter::PlaceOnTop(AActor* ActorToPlace, FHitResult HitSurface)
{
	//If surface invalid or too far away																		              //---> display message

	FVector HMin, HMax;

	//Get the bounding limits for our actor to place
	GetStaticMesh(ActorToPlace)->GetLocalBounds(Min, Max);

	//Check if the surface is a static map or an item
	if (HitSurface.GetActor()->ActorHasTag(FName(TEXT("Item"))))
	{
		GetStaticMesh(HitSurface.GetActor())->GetLocalBounds(HMin, HMax);
	}
	else
	{
		//Small offset to make sure objects are not colliding when we place them
		HMax.Z = 0.2;
	}

	//Check if the items are stackable together, and if so place them acordingly (copy rotation and match positioning)
	if (ActorToPlace->ActorHasTag(FName(TEXT("Stackable"))) && ActorToPlace->Tags == HitSurface.GetActor()->Tags)
	{
		GetStaticMesh(ActorToPlace)->SetWorldLocationAndRotation(HitSurface.GetActor()->GetActorLocation() + FVector(0.f, 0.f, HMax.Z - Min.Z), HitSurface.GetActor()->GetActorRotation());
	}
	else
	{
		GetStaticMesh(ActorToPlace)->SetWorldLocation(HitSurface.ImpactPoint + FVector(0.f, 0.f, HMax.Z) + HitSurface.Normal*((-Min) * GetStaticMesh(ActorToPlace)->GetComponentScale()));
	}

	//Reactivate the gravity and other properties which have been modified in order to permit manipulation
	GetStaticMesh(ActorToPlace)->SetEnableGravity(true);
	GetStaticMesh(ActorToPlace)->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	//Disable Rotation mode
	bRotationModeAllowed = false;
	//Reset rotation index to default 0
	RotationAxisIndex = 0;
}

void AMyCharacter::OpenCloseAction(AActor* OpenableActor)
{
	if (OpenableActor->GetName().Contains("Handle"))
	{
		OpenableActor = OpenableActor->GetAttachParentActor();
	}

	if (!AssetStateMap.Contains(OpenableActor))
	{
		return;
	}

	else
	{
		if (AssetStateMap.FindRef(OpenableActor) == EAssetState::Closed)
		{
			GetStaticMesh(OpenableActor)->AddImpulse(AppliedForce * OpenableActor->GetActorForwardVector());
			AssetStateMap.Add(OpenableActor, EAssetState::Open);
		}
		else if (AssetStateMap.FindRef(OpenableActor) == EAssetState::Open)
		{
			GetStaticMesh(OpenableActor)->AddImpulse(-AppliedForce * OpenableActor->GetActorForwardVector());
			AssetStateMap.Add(OpenableActor, EAssetState::Closed);
		}
		return;
	}
}

// Called every frame
void AMyCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//Draw a straight line in front of our character
	Start = MyCharacterCamera->GetComponentLocation();
	End = Start + MyCharacterCamera->GetForwardVector()*MaxGraspLength;
	HitObject = FHitResult(ForceInit);
	GetWorld()->LineTraceSingleByChannel(HitObject, Start, End, ECC_Pawn, TraceParams);

	//Mouse hovered behaviour with an empty hand
	if (!SelectedObject)
	{
		//Turn off the highlight effect when changing to another actor
		if (HighlightedActor && HitObject.GetActor() != HighlightedActor)
		{
			GetStaticMesh(HighlightedActor)->SetRenderCustomDepth(false);
			HighlightedActor = nullptr;
		}

		//Check if there is an object blocking the hit and if it is in our hand's range
		if (HitObject.bBlockingHit && HitObject.Distance < MaxGraspLength)
		{
			//Check if the object has interractive behaviour enabled
			if (AssetStateMap.Contains(HitObject.GetActor()) || AssetStateMap.Contains(HitObject.GetActor()->GetAttachParentActor()) || ItemMap.Contains(HitObject.GetActor()))
			{
				HighlightedActor = HitObject.GetActor();
				GetStaticMesh(HighlightedActor)->SetRenderCustomDepth(true);
			}
		}
	}

	//Behaviour for selected object in hand
	else
	{
		//Turn off the highlight effect because we can't pick up with this hand.
		if (HighlightedActor && HighlightedActor)
		{
			GetStaticMesh(HighlightedActor)->SetRenderCustomDepth(false);
			HighlightedActor = nullptr;
		}

		//Enable the player to access rotation mode
		bRotationModeAllowed = true;

	}

	//Draw object from the right hand
	if (RightHandSlot)
	{
		RightHandSlot->SetActorRotation(RightHandRotator + FRotator(0.f, GetActorRotation().Yaw, 0.f));
		GetStaticMesh(RightHandSlot)->SetWorldLocation(GetActorLocation() + FVector(20.f, 20.f, 20.f) * GetActorForwardVector() + FVector(RightYPos, RightYPos, RightYPos) * GetActorRightVector() + FVector(0.f, 0.f, RightZPos));

		//Add highlight if it is selected
		if (SelectedObject == RightHandSlot)
		{
			GetStaticMesh(RightHandSlot)->SetRenderCustomDepth(true);
		}
		else
		{
			GetStaticMesh(RightHandSlot)->SetRenderCustomDepth(false);
		}
	}

	//Draw object from the left hand
	if (LeftHandSlot)
	{
		LeftHandSlot->SetActorRotation(LeftHandRotator + FRotator(0.f, GetActorRotation().Yaw, 0.f));
		GetStaticMesh(LeftHandSlot)->SetWorldLocation(GetActorLocation() + FVector(20.f, 20.f, 20.f) * GetActorForwardVector() - FVector(LeftYPos, LeftYPos, LeftYPos) * GetActorRightVector() + FVector(0.f, 0.f, LeftZPos));

		//Add highlight if it is selected
		if (SelectedObject == LeftHandSlot)
		{
			GetStaticMesh(LeftHandSlot)->SetRenderCustomDepth(true);
		}
		else
		{
			GetStaticMesh(LeftHandSlot)->SetRenderCustomDepth(false);
		}
	}

	//Draw the stack held in hands if there is one
	if (TwoHandSlot.Num())
	{
		float LastItemOldZ = 0;
		float ZOffset = 0;
		
		for (AActor* StackItem : TwoHandSlot)
		{
			if (LastItemOldZ)
			{
				ZOffset += StackItem->GetActorLocation().Z - LastItemOldZ;
			}
			LastItemOldZ = StackItem->GetActorLocation().Z;

			StackItem->SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
			GetStaticMesh(StackItem)->SetWorldLocation(GetActorLocation() + FVector(8.f, 8.f, 8.f) * GetActorForwardVector() + FVector(0.f, 0.f, 20.f + ZOffset));
		}
		//Add highlight if it is selected
		if (TwoHandSlot.Contains(SelectedObject))
		{
			GetStaticMesh(SelectedObject)->SetRenderCustomDepth(true);
		}
		else
		{
			GetStaticMesh(SelectedObject)->SetRenderCustomDepth(false);
		}
	}
}

// Called to bind functionality to input
void AMyCharacter::SetupPlayerInputComponent(class UInputComponent* InputComponent)
{
	Super::SetupPlayerInputComponent(InputComponent);

	//// Default Camera view bindings
	InputComponent->BindAxis("LookUp", this, &AMyCharacter::AddControllerPitchInput);
	InputComponent->BindAxis("Turn", this, &AMyCharacter::AddControllerYawInput);

	//// Respond every frame to the values of our two movement axes, "MoveX" and "MoveY".
	InputComponent->BindAxis("MoveForward", this, &AMyCharacter::MoveForward);
	InputComponent->BindAxis("MoveRight", this, &AMyCharacter::MoveRight);

	//Set up the input from the mouse
	InputComponent->BindAction("Click", IE_Pressed, this, &AMyCharacter::Click);

	//Input from the tab button which switches selected hand
	InputComponent->BindAction("SwitchSelectedHand", IE_Pressed, this, &AMyCharacter::SwitchSelectedHand);

	//Key to switch between the axis the item turns around
	InputComponent->BindAction("SwitchRotationAxis", IE_Pressed, this, &AMyCharacter::SwitchRotationAxis);

	//Input from the mouse wheel to rotate the object
	InputComponent->BindAxis("RotateObject", this, &AMyCharacter::RotateObject);

	//Input frome keyboard arrows
	InputComponent->BindAxis("MoveItemZ", this, &AMyCharacter::MoveItemZ);
	InputComponent->BindAxis("MoveItemY", this, &AMyCharacter::MoveItemY);

	//Right mouse click which just calls the GrabWithTwoHandsMethod
	InputComponent->BindAction("GrabWithTwoHands", IE_Pressed, this, &AMyCharacter::GrabWithTwoHands);

	//Pause game
	InputComponent->BindAction("Pause", IE_Pressed, this, &AMyCharacter::Pause);
}

void AMyCharacter::MoveForward(const float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// Find out which way is forward
		FRotator Rotation = Controller->GetControlRotation();
		// Limit pitch when walking or falling
		if (GetCharacterMovement()->IsMovingOnGround() || GetCharacterMovement()->IsFalling())
		{
			Rotation.Pitch = 0.0f;
		}
		// add movement in that direction
		const FVector Direction = FRotationMatrix(Rotation).GetScaledAxis(EAxis::X);
		AddMovementInput(Direction, Value *0.5);
	}
}

void AMyCharacter::MoveRight(const float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FVector Direction = FRotationMatrix(Rotation).GetScaledAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value *0.5);
	}
}

void AMyCharacter::SwitchSelectedHand()
{
	if (TwoHandSlot.Num())
	{
		return;
	}

	bRightHandSelected = !bRightHandSelected;
	if (bRightHandSelected)
	{
		SelectedObject = RightHandSlot;
		//UE_LOG(LogTemp, Warning, TEXT("Our character will perform the next action with his RIGHT hand"));					    //---> display message
	}
	else
	{
		SelectedObject = LeftHandSlot;
		//UE_LOG(LogTemp, Warning, TEXT("Our character will perform the next action with his LEFT hand"));						 //---> display message
	}

	//Exit rotation mode
	RotationAxisIndex = 0;
}

void AMyCharacter::Pause()
{

}

void AMyCharacter::Click()
{
	//Exit function call if invalid apelation
	if (!HitObject.IsValidBlockingHit() || HitObject.Distance > MaxGraspLength)
	{
		PopUpMessage = ActionNotValid;																	//---> display message
		return;                                                                                                                    
	}
	//Behaviour when we want to drop the item currently held in hand
	if (SelectedObject)
	{
		//Drops our currently selected item on the surface clicked on
		DropFromInventory(SelectedObject, HitObject);
	}

	//Behaviour when wanting to grab an item or opening/closing actions
	else if (HighlightedActor)
	{
		//Section for items that can be picked up and moved around
		if (ItemMap.Contains(HighlightedActor))
		{
			//Picks up the focused item
			PickToInventory(HighlightedActor);
		}

		//Section for openable actors
		else
		{
			OpenCloseAction(HighlightedActor);
		}
	}
}

void AMyCharacter::PickToInventory(AActor* CurrentObject)
{
	//Check if item is pickable (doesn't have any other on top of it)
	if (HasAnyOnTop(CurrentObject))
	{
		//UE_LOG(LogTemp, Warning, TEXT("You can't do that because it has something on it!"));                                             //---> display message           
		PopUpMessage = SmthOnTop;
		return;
	}

	//Get the stack which contains this item in order to grab the topmost item only
	TSet<AActor*> LocalStackVariable = GetStack(CurrentObject);

	//Change the referenced of the selected object to the one we actually manipulate
	SelectedObject = CurrentObject;
	
	//Change the outline collor effect to orange
	GetStaticMesh(CurrentObject)->SetCustomDepthStencilValue(2);
	
	//Add a reference to the object in the correct item slot (left or right hand) and save the value of it's rotation
	if (bRightHandSelected)
	{
		RightHandSlot = CurrentObject;
		RightHandRotator = RightHandSlot->GetActorRotation() - GetActorRotation();
	}
	else
	{
		LeftHandSlot = CurrentObject;
		LeftHandRotator = LeftHandSlot->GetActorRotation() - GetActorRotation();
	}

	//Deactivate the gravity and collision
	GetStaticMesh(CurrentObject)->SetEnableGravity(false);
	GetStaticMesh(CurrentObject)->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	//Ignore clicking on item if held in hand
	TraceParams.AddIgnoredComponent(GetStaticMesh(CurrentObject));
}

void AMyCharacter::DropFromInventory(AActor* CurrentObject, FHitResult HitSurface)
{
	if (!HitSurface.IsValidBlockingHit())
	{
		//UE_LOG(LogTemp, Warning, TEXT("You need to get closer\nin order to do that!"));                                                //---> display message              
		PopUpMessage = ActionNotValid;
		return;
	}
	if (HitSurface.Distance > MaxGraspLength)
	{
		PopUpMessage = GetCloser;
		return;
	}

	//Case in which we are holding a stack
	if (TwoHandSlot.Num())
	{
		bool bFirstLoop = true;
		FVector WorldPositionChange = FVector(0.f, 0.f, 0.f);

		for (auto Iterator : TwoHandSlot)
		{
			GetStaticMesh(Iterator)->SetEnableGravity(true);
			GetStaticMesh(Iterator)->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

			if (!bFirstLoop)
			{
				GetStaticMesh(Iterator)->SetWorldLocation(Iterator->GetActorLocation() - WorldPositionChange);
			}

			else
			{
				WorldPositionChange = Iterator->GetActorLocation();
				PlaceOnTop(Iterator, HitSurface);
				WorldPositionChange = WorldPositionChange - Iterator->GetActorLocation();
				bFirstLoop = false;
			}
		}
		TwoHandSlot.Empty();
		GetStaticMesh(SelectedObject)->SetCustomDepthStencilValue(1);
		SelectedObject = nullptr;
		return;
	}

	PlaceOnTop(CurrentObject, HitSurface);

	//Reset ignored parameters
	TraceParams.ClearIgnoredComponents();

	//Remove the reference to the object because we are not holding it any more
	if (bRightHandSelected)
	{
		RightHandSlot = nullptr;

		//Reset the positioning vector back to default state
		RightZPos = 30.f;
		RightYPos = 20;

		//Add item in left hand back to ignored actor by line trace
		if (LeftHandSlot)
		{
			TraceParams.AddIgnoredComponent(GetStaticMesh(LeftHandSlot));
		}
	}
	else
	{
		LeftHandSlot = nullptr;
		//Reset the positioning vector back to default state
		LeftZPos = 30.f;
		LeftYPos = 20;

		//Add item in right hand back to ignored actor by line trace
		if (RightHandSlot)
		{
			TraceParams.AddIgnoredComponent(GetStaticMesh(RightHandSlot));
		}
	}

	//Reset outline color to blue
	GetStaticMesh(SelectedObject)->SetCustomDepthStencilValue(1);
	//Remove the reference because we just dropped the item that was selected
	SelectedObject = nullptr;
}

void AMyCharacter::SwitchRotationAxis()
{
	//Exit function call if rotation is not permited here
	if (!bRotationModeAllowed)
	{
		return;
	}
	//Increment the index which coresponds to rotation axis
	RotationAxisIndex++;

	//Recycle after the third axis
	if (RotationAxisIndex == 4)
	{
		RotationAxisIndex = 1;
	}
}

void AMyCharacter::RotateObject(const float Value)
{
	//Check if controler is valid, input is not null and that we are in rotation mode
	if ((Controller != nullptr) && (Value != 0.0f) && RotationAxisIndex)
	{
		//Local variable to store the difference gained from the input
		FRotator RotIncrement;

		//Add value to increment based on the axis
		switch (RotationAxisIndex)
		{
		case 1:
			RotIncrement = FRotator(0.f, Value*10.f, 0.f);
			break;
		case 2:
			RotIncrement = FRotator(0.f, 0.f, Value*10.f);
			break;
		case 3:
			RotIncrement = FRotator(Value*10.f, 0.f, 0.f);
			break;
		default:
			return;
		}

		//Add our input to the currently selected object
		if (bRightHandSelected)
		{
			RightHandRotator += RotIncrement;
		}
		else
		{
			LeftHandRotator += RotIncrement;
		}
	}
}

void AMyCharacter::MoveItemZ(const float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		if (bRightHandSelected)
		{
			if ((Value < 0 && RightZPos > 20) || (Value >0 && RightZPos <40))
			{
				RightZPos += Value*0.35f;
			}
		}
		else
		{
			if ((Value < 0 && LeftZPos > 20) || (Value >0 && LeftZPos <40))
			{
				LeftZPos += Value*0.35f;
			}
		}
	}
}

void AMyCharacter::MoveItemY(const float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		if (bRightHandSelected)
		{
			if ((Value < 0 && RightYPos > 5) || (Value >0 && RightYPos <25))
			{
				RightYPos += Value*0.35f;
			}
		}
		else
		{
			if ((Value < 0 && LeftYPos <25) || (Value >0 && LeftYPos >5))
			{
				LeftYPos -= Value*0.35f;
			}
		}
	}
}

bool AMyCharacter::HasAnyOnTop(const AActor* CheckActor)
{
	GetStaticMesh(CheckActor)->GetLocalBounds(Min, Max);
	FVector LowBound = CheckActor->GetActorLocation() + Min;
	FVector HighBound = CheckActor->GetActorLocation() + Max;
	FVector HMin, HMax;

	//Loop through the list of items and check if it's location is on top of our item of interest
	for (const auto Item : ItemMap)
	{
		FVector IteratorLocation = Item.Key->GetActorLocation();

		if (Item.Key != CheckActor &&
			(LowBound.X < IteratorLocation.X && HighBound.X > IteratorLocation.X) &&
			(LowBound.Y < IteratorLocation.Y && HighBound.Y > IteratorLocation.Y) &&
			(CheckActor->GetActorLocation().Z + Min.Z< IteratorLocation.Z && HighBound.Z + 15 > IteratorLocation.Z))
		{
			GetStaticMesh(Item.Key)->GetLocalBounds(HMin, HMax);
			if (IteratorLocation.Z + HMin.Z > CheckActor->GetActorLocation().Z + Min.Z)
			{
				return true;                                                                                        
			}
		}
	}
	return false;
}




