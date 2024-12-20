// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintMergeLibrary.generated.h"


/**
 * 
 */
UCLASS()
class BLUEPRINTMERGETEST_API UBlueprintMergeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ブループリントをマージする
	UFUNCTION(BlueprintCallable)
	static void MergeBlueprint(UObject* WorldContextObject, UBlueprint* Base, UBlueprint* Left, UBlueprint* Right, const FString& OutputName);

private:


	struct FPropertyData
	{
		FPropertyData(const FProperty* InProperty, const void* InContainer)
			: Property(InProperty)
			, Container(InContainer)
		{
		}

		const FProperty* Property;
		const void* Container;
	};

	enum class EDiffType
	{
		None,
		Add,
		Remove,
		Modify,
		Confilict,
	};

	// プロパティの差分情報
	struct FDiffData
	{
		/// <summary>
		/// 
		/// </summary>
		/// <param name="PropertyPath">プロパティパス</param>
		/// <param name="InDiffType">差分の種類</param>
		/// <param name="bInIsLeftUpdate">Left に差分があるか</param>
		/// <param name="bInIsRightUpdate">Right に差分があるか</param>
		FDiffData(const FName& PropertyPath, EDiffType InDiffType, bool bInIsLeftUpdate, bool bInIsRightUpdate)
			: Path(PropertyPath)
			, DiffType(InDiffType)
			, bIsLeftUpdata(bInIsLeftUpdate)
			, bIsRightUpdate(bInIsRightUpdate)
		{
		}

		/// <summary>
		/// 差分がないか
		/// </summary>
		/// <returns></returns>
		bool IsNoDifference() const
		{
			return !bIsLeftUpdata && !bIsRightUpdate;
		}

		bool IsLeftUpdate() const
		{
			return bIsLeftUpdata;
		}

		bool IsRightUpdate() const
		{
			return bIsRightUpdate;
		}

		EDiffType GetDiffType() const
		{
			return DiffType;
		}
	private:
		FName Path;
		EDiffType DiffType = EDiffType::None;
		bool bIsLeftUpdata = false;
		bool bIsRightUpdate = false;
	};

	struct FSCSNodeData
	{
		FSCSNodeData(class USCS_Node* InNode)
			: Node(InNode)
		{
		}

		class USCS_Node* Node;
	};

	enum class EBuildPropertyMapOption : uint8
	{
		None = 0,
		IncludeCompositeType = 1 << 0,
	};

	enum class EGraphType : uint8
	{
		None,
		Function,
		Event,
		Macro,
		Delegate,
		Ubergraph,
	};

	// プロパティマップを構築する
	static TMap<FName, FPropertyData> BuildPropertyMap(UObject* Target, EBuildPropertyMapOption Flags = EBuildPropertyMapOption::None);
	static TMap<FName, class USCS_Node*> BuildSCSNodeMap(UBlueprintGeneratedClass* BPGC);
	static void BuildSCSNodeMapRecursive(class USCS_Node* Node, const FString& Path, TMap<FName, class USCS_Node*>& InOutMap);
	static TMap<FName, class UEdGraph*> BuildGraphMap(UBlueprint* Blueprint, EGraphType Type);
	static void BuildGraphMapRecursive(UEdGraph* Graph, const FString& Path, TMap<FName, UEdGraph*>& InOutMap);
	static TMap<FName, class UEdGraphNode*> BuildGraphNodesMap(UEdGraph* Graph);
	static TMap<FName, class UEdGraphPin*> BuildGraphPinsMap(UEdGraphNode* Node);
	static FString GetObjectPath(UObject* Root, UObject* Object, bool bRequiredRootName = false);

	static void MergeBlueprintMemberVariables(UBlueprint* Base,
		const TMap<FName, FPropertyData>& BasePropertyMap,
		UBlueprint* Left,
		const TMap<FName, FPropertyData>& LeftPropertyMap,
		UBlueprint* Right, 
		const TMap<FName, FPropertyData>& RightPropertyMap,
		const TMap<FName, FDiffData>& DiffPropertyMap,
		const TSet<FName>& UnionPropertyKeys, 
		const TMap<FName, FPropertyData>& MergedPropertyMap,
		UBlueprint* InOutMergedBlueprint);

	static void MergeObjectProperties(UObject* Base, UObject* Left, UObject* Right, UObject* InOutMergedObject);

	static void MergeComponentProperties(FPropertyData& BaseComponentProperty, FPropertyData& LeftComponentProperty, FPropertyData& RightComponentProperty, FPropertyData& InOutMergedComponentProperty);

	static void MergeBlueprintComponents(UBlueprint* Base, UBlueprint* Left, UBlueprint* Right, UBlueprint* InOutMergedBlueprint);

	static void MergeFunctionGraphs(UBlueprint* Base, UBlueprint* Left, UBlueprint* Right, UBlueprint* InOutMergedBlueprint, EGraphType Type);

	// グラフの内容が一致するか
	// 差分があったプロパティのリストを返す
	static bool IdenticalGraphs(UEdGraph* LeftGraph, UEdGraph* RightGraph, TArray<FName>& OutDiffProperties);
	static bool IdenticalNodes(UEdGraph* LeftGraph, UEdGraphNode* LeftNode, UEdGraph* RightGraph, UEdGraphNode* RightNode);
	static bool IdenticalPins(UEdGraphPin* LeftPin, UEdGraphPin* RightPin);

	static bool IdenticalProperties(UObject* LeftRootObject, const FPropertyData& Left, UObject* RightRootObject, const FPropertyData& Right);

	// プロパティの値をログ出力する
	static void OutputPropertyValues(const FPropertyData& PropertyData, const FString& ContextString);

	// グラフタイプに応じたグラフを追加する
	static void AddGraphToBlueprint(UBlueprint* Blueprint, UEdGraph* Graph, EGraphType Type);
};
