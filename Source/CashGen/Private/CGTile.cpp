#include "CGTile.h"
#include "Components/StaticMeshComponent.h"
#include "Struct/CGTerrainConfig.h"

#include "ProceduralMeshComponent.h"
#include "TTileGenerator.h"
#include "Components/BaseDynamicMeshSceneProxy.h"

DECLARE_CYCLE_STAT(TEXT("CashGenStat ~ RMCUpdate"), STAT_RMCUpdate, STATGROUP_CashGenStat);

ACGTile::ACGTile()
{
	PrimaryActorTick.bCanEverTick = false;

	SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("RootComponent"));
	RootComponent = SphereComponent;

	Tags.Add(FName("Landscape")); // Add a new tag
	
	CurrentLOD = 10;
	PreviousLOD = 10;

	mySector = FCGIntVector2(0, 0);
}

ACGTile::~ACGTile()
{
	if (myRegion)
	{
		delete myRegion;
		myRegion = nullptr;
	}
}

int32 ACGTile::GetNumMeshTransforms() const
{
	return MeshTransforms.Num();
}

void ACGTile::SetMeshTransforms(const TArray<FVector>& Vertices, const TArray<FVector>& Normals){
	int32 VtxNum = Vertices.Num();
	FVector WorldLocation = GetActorLocation();
	if ((VtxNum > 0) && (Normals.Num() == VtxNum)){
		for (int32 w = 0; w < VtxNum; w++) {
			FVector Pos = Vertices[w] + WorldLocation;
			FVector Normal =  Normals[w] + FVector(0.0f, 0.0f, 1.0f);
			FTransform OutTransform = FTransform(Normal.ToOrientationQuat(),Pos,  FVector(1, 1, 1));
			MeshTransforms.Add(OutTransform);
		}
	}
}

const TArray<FTransform>& ACGTile::GetMeshTransforms(){
	return MeshTransforms;
}

bool ACGTile::TickTransition(float DeltaSeconds)
{
	for (auto& lod : LODStatus)
	{
		if (lod.Value == ELODStatus::TRANSITION && MaterialInstances.Num() > 0)
		{
			if (LODTransitionOpacity >= -1.0f)
			{
				LODTransitionOpacity -= DeltaSeconds;

				if (LODTransitionOpacity > 0.0f)
				{
					MaterialInstances[lod.Key]->SetScalarParameterValue(FName("TerrainOpacity"), 1.0f - LODTransitionOpacity);
				}
				else if (PreviousLOD != 10 && PreviousLOD != CurrentLOD)
				{
					MaterialInstances[PreviousLOD]->SetScalarParameterValue(FName("TerrainOpacity"), LODTransitionOpacity + 1.0f);
				}
			}
			else
			{
				if (PreviousLOD != 10 && PreviousLOD != CurrentLOD)
				{
					MeshComponents[PreviousLOD]->SetVisibility(false);
				}

				LODTransitionOpacity = 1.0f;
				lod.Value = ELODStatus::CREATED;
				return true;
			}
		}
	}
	return false;
}

/************************************************************************
 * Move the tile and make it hidden pending a redraw
 ************************************************************************/
void ACGTile::RepositionAndHide(uint8 aNewLOD)
{
	SetActorLocation(FVector((TerrainConfigMaster->TileXUnits * TerrainConfigMaster->UnitSize * mySector.X) - TerrainConfigMaster->TileOffset.X, (TerrainConfigMaster->TileYUnits * TerrainConfigMaster->UnitSize * mySector.Y) - TerrainConfigMaster->TileOffset.Y, 0.0f));

	SetActorHiddenInGame(true);

	CurrentLOD = aNewLOD;
}

void ACGTile::BeginPlay()
{
	Super::BeginPlay();
}

void ACGTile::Tick(float DeltaSeconds)
{
}

/************************************************************************
 * Initial setup of the tile, creates components and material instance
 ************************************************************************/
void ACGTile::UpdateSettings(FCGIntVector2 aOffset, FCGTerrainConfig* aTerrainConfig, FVector aWorldOffset)
{
    mySector.X = aOffset.X;
    mySector.Y = aOffset.Y;

    if (!IsInitalized)
    {
        WorldOffset = aWorldOffset;
        TerrainConfigMaster = aTerrainConfig;

        SetActorTickEnabled(TerrainConfigMaster->DitheringLODTransitions && aTerrainConfig->LODs.Num() > 1);

        // Water component remains the same
        FString waterCompName = "WaterSMC";
        FTransform waterTransform = FTransform(FRotator::ZeroRotator, FVector(TerrainConfigMaster->TileXUnits * TerrainConfigMaster->UnitSize * 0.5f, TerrainConfigMaster->TileXUnits * TerrainConfigMaster->UnitSize * 0.5f, 0.0f), FVector(TerrainConfigMaster->TileXUnits * TerrainConfigMaster->UnitSize * 0.01f, TerrainConfigMaster->TileYUnits * TerrainConfigMaster->UnitSize * 0.01f, 1.0f));
        MyWaterMeshComponent = NewObject<UStaticMeshComponent>(this, UStaticMeshComponent::StaticClass(), *waterCompName);
        MyWaterMeshComponent->SetStaticMesh(TerrainConfigMaster->WaterMesh);
        MyWaterMeshComponent->SetRelativeTransform(waterTransform);
        MyWaterMeshComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
        MyWaterMeshComponent->RegisterComponent();

        myWaterMaterialInstance = UMaterialInstanceDynamic::Create(TerrainConfigMaster->WaterMaterialInstance, this);
        MyWaterMeshComponent->SetMaterial(0, myWaterMaterialInstance);

        // Create DynamicMeshComponents for each LOD
        for (int32 i = 0; i < aTerrainConfig->LODs.Num(); ++i)
        {
            FString compName = "DMC" + FString::FromInt(i);
            UDynamicMeshComponent* DynMeshComp = NewObject<UDynamicMeshComponent>(this, UDynamicMeshComponent::StaticClass(), *compName);
            DynMeshComp->SetRelativeTransform(FTransform());
            DynMeshComp->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);

            // Configure collision
            DynMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            DynMeshComp->SetCollisionObjectType(ECC_WorldDynamic);
            DynMeshComp->SetCollisionResponseToAllChannels(ECR_Block);
            DynMeshComp->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);

            // Configure shadows
            DynMeshComp->SetCastShadow(i == 0 ? TerrainConfigMaster->CastShadows : false);

            // Enable complex collision for physics
            DynMeshComp->SetComplexAsSimpleCollisionEnabled(true, false);

            MeshComponents.Add(i, DynMeshComp);
            LODStatus.Add(i, ELODStatus::NOT_CREATED);

            // Create material instances
            if (TerrainConfigMaster->TerrainMaterialInstance && !TerrainConfigMaster->MakeDynamicMaterialInstance)
            {
                MaterialInstance = TerrainConfigMaster->TerrainMaterialInstance;
                DynMeshComp->SetMaterial(0, MaterialInstance);
            }
            else if (TerrainConfigMaster->TerrainMaterialInstance && TerrainConfigMaster->MakeDynamicMaterialInstance)
            {
                MaterialInstances.Add(i, UMaterialInstanceDynamic::Create(TerrainConfigMaster->TerrainMaterialInstance, this));
                DynMeshComp->SetMaterial(0, MaterialInstances[i]);
            }
        }

        // ... rest of initialization code remains the same ...

        IsInitalized = true;
    }
}

/************************************************************************
 *  Draw a simple quad to use as the water plane
 ************************************************************************/
bool ACGTile::CreateWaterMesh()
{

	if (MeshComponents.Num() > 0 && MeshComponents.Contains(0))
	{
		UDynamicMeshComponent* DynMeshComp = MeshComponents[0];
		UDynamicMesh* WaterMesh = NewObject<UDynamicMesh>(DynMeshComp);
		FDynamicMesh3 Mesh;

		// Create water quad geometry
		TArray<int32> VertexIDs;
		TArray<FVector> Positions = {
			FVector(0.0f, 0.0f, 0.0f),
			FVector(0.0f, TerrainConfigMaster->TileYUnits * TerrainConfigMaster->UnitSize, 0.0f),
			FVector(TerrainConfigMaster->TileXUnits * TerrainConfigMaster->UnitSize, TerrainConfigMaster->TileYUnits * TerrainConfigMaster->UnitSize, 0.0f),
			FVector(TerrainConfigMaster->TileXUnits * TerrainConfigMaster->UnitSize, 0.0f, 0.0f)
		};

		for (const FVector& Pos : Positions)
		{
			VertexIDs.Add(Mesh.AppendVertex(Pos));
		}

		// Add two triangles for the quad
		Mesh.AppendTriangle(VertexIDs[0], VertexIDs[1], VertexIDs[2]);
		Mesh.AppendTriangle(VertexIDs[2], VertexIDs[3], VertexIDs[0]);

		WaterMesh->SetMesh(MoveTemp(Mesh));
		DynMeshComp->SetDynamicMesh(WaterMesh);
		DynMeshComp->NotifyMeshUpdated();
		DynMeshComp->ComponentTags.Add(FName("Water"));

		myWaterMaterialInstance = UMaterialInstanceDynamic::Create(TerrainConfigMaster->WaterMaterialInstance, this);
		DynMeshComp->SetMaterial(0, myWaterMaterialInstance);

		return true;
	}
	return false;
}

/************************************************************************
  *  Updates the mesh for a given LOD and starts the transition effects  
  ************************************************************************/
void ACGTile::UpdateMesh(uint8 aLOD, bool aIsInPlaceUpdate,
    TArray<FVector>& aPositions, TArray<FVector>& aNormals, TArray<FProcMeshTangent>& aTangents, TArray<FVector2D>& aUV0s, TArray<FColor>& aColours, TArray<int32>& aTriangles, TArray<FColor>& aTextureData)
{
    SCOPE_CYCLE_COUNTER(STAT_RMCUpdate);
    SetActorHiddenInGame(false);

    PreviousLOD = CurrentLOD;
    CurrentLOD = aLOD;
    LODTransitionOpacity = 1.0f;
    SetMeshTransforms(aPositions, aNormals);
	
    for (int32 i = 0; i < TerrainConfigMaster->LODs.Num(); ++i)
    {
        if (i == aLOD && MeshComponents.Contains(i)){
            UDynamicMeshComponent* DynMeshComp = MeshComponents[i];
            
            if (LODStatus[i] == ELODStatus::NOT_CREATED)
            {
            	
                // Create new dynamic mesh
            	UTTileGenerator* TileGenerator = NewObject<UTTileGenerator>();
                UDynamicMesh* NewMesh = NewObject<UDynamicMesh>(DynMeshComp);
            	NewMesh->GetMeshPtr();
            	NewMesh->bEnableMeshGenerator = true;
            	NewMesh->SetMeshGenerator(TileGenerator);
                FDynamicMesh3 Mesh;
                
                // Add vertices
                TArray<int32> VertexIDs;
                for (const FVector& Pos : aPositions)
                {
                    VertexIDs.Add(Mesh.AppendVertex(Pos));
                }

                // Add triangles
                for (int32 TriIdx = 0; TriIdx < aTriangles.Num(); TriIdx += 3)
                {
                    Mesh.AppendTriangle(aTriangles[TriIdx], aTriangles[TriIdx + 1], aTriangles[TriIdx + 2]);
                }

                // Set normals if available
                if (aNormals.Num() == aPositions.Num() && Mesh.HasVertexNormals())
                {
                    for (int32 v = 0; v < aPositions.Num(); ++v)
                    {
                        Mesh.SetVertexNormal(VertexIDs[v], FVector3f(aNormals[v]));
                    }
                }

// Set UVs if available
if (aUV0s.Num() == aPositions.Num() && Mesh.Attributes()->GetUVLayer(0) != nullptr)
{
    FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->GetUVLayer(0);
    for (int32 v = 0; v < aPositions.Num(); ++v)
    {
        UVOverlay->SetElement(VertexIDs[v], FVector2f(aUV0s[v]));  // Cast FVector2D to FVector2f
    }
}

                // Set vertex colors if available
                if (aColours.Num() == aPositions.Num() && Mesh.Attributes()->PrimaryColors() != nullptr)
                {
                    FDynamicMeshColorOverlay* ColorOverlay = Mesh.Attributes()->PrimaryColors();
                    for (int32 v = 0; v < aPositions.Num(); ++v)
                    {
                        ColorOverlay->SetElement(VertexIDs[v], FVector4f(aColours[v]));
                    }
                }

                NewMesh->SetMesh(MoveTemp(Mesh));
                DynMeshComp->SetDynamicMesh(NewMesh);
                DynMeshComp->NotifyMeshUpdated();

                LODStatus[i] = ELODStatus::TRANSITION;
                DynMeshComp->ComponentTags.Add(FName("Landscape"));
            }
            else
            {
                // Update existing mesh
                DynMeshComp->EditMesh([&aPositions, &aNormals, &aColours, &aUV0s](FDynamicMesh3& Mesh)
                {
                    for (int32 v = 0; v < aPositions.Num() && v < Mesh.VertexCount(); ++v)
                    {
                        Mesh.SetVertex(v, aPositions[v]);
                        if (Mesh.HasVertexNormals())
                        {
                            Mesh.SetVertexNormal(v, (aNormals.IsValidIndex(v) ? FVector3f(aNormals[v]) : FVector3f::UpVector));
                        }
                    }
                });

                DynMeshComp->NotifyMeshVertexAttributesModified(true, true, true, true);
                LODStatus[i] = ELODStatus::TRANSITION;
            }

            DynMeshComp->SetVisibility(true);
            DynMeshComp->SetCollisionEnabled(TerrainConfigMaster->LODs[aLOD].isCollisionEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        }
        else if (!aIsInPlaceUpdate && MeshComponents.Contains(i))
        {
            MeshComponents[i]->SetVisibility(false);
        }
    }

    // Handle splat map updates
    if (aLOD == 0 && TerrainConfigMaster->GenerateSplatMap && TerrainConfigMaster->MakeDynamicMaterialInstance && MaterialInstances.Num() > 0)
    {
        myTexture->UpdateTextureRegions(0, 1, myRegion, 4 * TerrainConfigMaster->TileXUnits, 4, (uint8*)aTextureData.GetData());

        MaterialInstances[0]->SetTextureParameterValue("SplatMap", myTexture);
        myWaterMaterialInstance->SetTextureParameterValue("SplatMap", myTexture);
    }

    // Update water collision
    if (TerrainConfigMaster->LODs[aLOD].isCollisionEnabled)
    {
        MyWaterMeshComponent->SetCollisionEnabled(TerrainConfigMaster->WaterCollision);
    }
    else
    {
        MyWaterMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

UMaterialInstanceDynamic* ACGTile::GetMaterialInstanceDynamic(const uint8 aLOD)
{
	if (aLOD < MaterialInstances.Num() - 1)
	{
		return MaterialInstances[aLOD];
	}

	return nullptr;
}
