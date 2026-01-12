// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UDynamicMesh.h"
#include "TTileGenerator.generated.h"

/**
 * 
 */
UCLASS()
class CASHGEN_API UTTileGenerator : public UDynamicMeshGenerator
{
	GENERATED_BODY()
	
	virtual void Generate(FDynamicMesh3& MeshInOut) override;
	
	
};
