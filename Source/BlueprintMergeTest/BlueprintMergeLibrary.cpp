// Fill out your copyright notice in the Description page of Project Settings.


#include "BlueprintMergeLibrary.h"
#include <regex>
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet/KismetSystemLibrary.h"
#include "AssetToolsModule.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraph/EdGraphPin.h"


UE_DISABLE_OPTIMIZATION

void UBlueprintMergeLibrary::MergeBlueprint(UObject* WorldContextObject, UBlueprint* Base, UBlueprint* Left, UBlueprint* Right, const FString& OutputName)
{
	if (!Base || !Left || !Right)
	{
		return;
	}

	// ブループリントをマージ
	UBlueprint* OutputBlueprint = DuplicateObject(Base, WorldContextObject, FName(*OutputName));

	TMap<FName, FPropertyData> BasePropertyMap = BuildPropertyMap(Base->GeneratedClass->GetDefaultObject());
	TMap<FName, FPropertyData> LeftPropertyMap = BuildPropertyMap(Left->GeneratedClass->GetDefaultObject());
	TMap<FName, FPropertyData> RightPropertyMap = BuildPropertyMap(Right->GeneratedClass->GetDefaultObject());

	// プロパティ名をログ出力
	//for (const TPair<FName, FPropertyData>& Pair : BasePropertyMap)
	//{
	//	UE_LOG(LogTemp, Log, TEXT("Base: %s"), *Pair.Key.ToString());
	//}
	//for (const TPair<FName, FPropertyData>& Pair : LeftPropertyMap)
	//{
	//	UE_LOG(LogTemp, Log, TEXT("Left: %s"), *Pair.Key.ToString());
	//}
	//for (const TPair<FName, FPropertyData>& Pair : RightPropertyMap)
	//{
	//	UE_LOG(LogTemp, Log, TEXT("Right: %s"), *Pair.Key.ToString());
	//}

	TMap<FName, FDiffData> DiffPropertyMap;

	// マップの要素を結合する

	// キーを統合する
	TSet<FName> UnionPropertyKeys;
	{
		TSet<FName> Keys;
		BasePropertyMap.GetKeys(Keys);
		UnionPropertyKeys = UnionPropertyKeys.Union(Keys);
	
		LeftPropertyMap.GetKeys(Keys);
		UnionPropertyKeys = UnionPropertyKeys.Union(Keys);
		
		RightPropertyMap.GetKeys(Keys);
		UnionPropertyKeys = UnionPropertyKeys.Union(Keys);
	}


	for (const FName& PropertyPath : UnionPropertyKeys)
	{
		const FPropertyData* BasePropertyData = BasePropertyMap.Find(PropertyPath);
		const FPropertyData* LeftPropertyData = LeftPropertyMap.Find(PropertyPath);
		const FPropertyData* RightPropertyData = RightPropertyMap.Find(PropertyPath);

		EDiffType DiffType = EDiffType::None;
		bool bIsLeftUpdate = false;
		bool bIsRightUpdate = false;

		if (BasePropertyData)
		{
			if (LeftPropertyData && RightPropertyData)
			{
				// 差分があるかチェック
				if (!BasePropertyData->Property->Identical(BasePropertyData->Container, LeftPropertyData->Container))
				{
					DiffType = EDiffType::Modify;
					bIsLeftUpdate = true;
				}
				if (!BasePropertyData->Property->Identical(BasePropertyData->Container, RightPropertyData->Container))
				{
					DiffType = EDiffType::Modify;
					bIsRightUpdate = true;
				}
			}
			
			if (!LeftPropertyData || !RightPropertyData)
			{
				DiffType = EDiffType::Remove;
				bIsLeftUpdate = !!LeftPropertyData;
				bIsRightUpdate = !!RightPropertyData;
			}
		}
		else
		{
			if (LeftPropertyData || RightPropertyData)
			{
				DiffType = EDiffType::Add;
				bIsLeftUpdate = !!LeftPropertyData;
				bIsRightUpdate = !!RightPropertyData;
			}
		}

		if (!bIsLeftUpdate && !bIsRightUpdate)
		{
			// 差分がない場合はスキップ
			continue;
		}

		DiffPropertyMap.Emplace(PropertyPath, FDiffData(PropertyPath, DiffType, bIsLeftUpdate, bIsRightUpdate));
	}

	IAssetTools& AssetTool = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	FAssetData AssetData(Base);

	FString OutputPath = AssetData.PackagePath.ToString() / OutputName;
	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
	if (EditorAssetSubsystem->DoesAssetExist(*OutputPath))
	{
		// すでにアセットが存在する場合は削除
		EditorAssetSubsystem->DeleteAsset(*OutputPath);
	}

	if (UBlueprint* MergedBlueprint =  Cast<UBlueprint>(AssetTool.DuplicateAsset(OutputName, AssetData.PackagePath.ToString(), Base)))
	{
		// プロパティを更新
		TMap<FName, FPropertyData> MergedAssetPropertyMap = BuildPropertyMap(MergedBlueprint->GeneratedClass->GetDefaultObject());

		MergeBlueprintMemberVariables(Base, BasePropertyMap, Left, LeftPropertyMap, Right, RightPropertyMap, DiffPropertyMap, UnionPropertyKeys, MergedAssetPropertyMap, MergedBlueprint);
		
		// ブループリントをコンパイルして、デフォルトオブジェクトを再生成してから、再度プロパティマップを構築する
		FKismetEditorUtilities::CompileBlueprint(MergedBlueprint);
		MergedAssetPropertyMap = BuildPropertyMap(MergedBlueprint->GeneratedClass->GetDefaultObject());

		for (TPair<FName, FDiffData>& Pair : DiffPropertyMap)
		{
			FName PropertyPath = Pair.Key;
			const FDiffData& DiffPropertyData = Pair.Value;

			const FPropertyData* LeftPropertyData = LeftPropertyMap.Find(PropertyPath);
			const FPropertyData* RightPropertyData = RightPropertyMap.Find(PropertyPath);
			const FPropertyData* MergedPropertyData = MergedAssetPropertyMap.Find(PropertyPath);

			if (DiffPropertyData.IsNoDifference())
			{
				// 差分がない場合はスキップ
				continue;
			}

			if (DiffPropertyData.IsLeftUpdate() && DiffPropertyData.IsRightUpdate())
			{
				// コンフリクト
				continue;
			}

			if (MergedPropertyData)
			{
				if (DiffPropertyData.IsLeftUpdate())
				{
					MergedPropertyData->Property->CopyCompleteValue(const_cast<void*>(MergedPropertyData->Container), LeftPropertyData->Container);
				}
				else if (DiffPropertyData.IsRightUpdate())
				{
					MergedPropertyData->Property->CopyCompleteValue(const_cast<void*>(MergedPropertyData->Container), RightPropertyData->Container);
				}
			}
		}

		// コンポーネントをマージ
		MergeBlueprintComponents(Base, Left, Right, MergedBlueprint);

		// 各種グラフをマージ
		MergeFunctionGraphs(Base, Left, Right, MergedBlueprint, EGraphType::Function);
		//MergeFunctionGraphs(Base, Left, Right, MergedBlueprint, EGraphType::Event);
		MergeFunctionGraphs(Base, Left, Right, MergedBlueprint, EGraphType::Macro);
		MergeFunctionGraphs(Base, Left, Right, MergedBlueprint, EGraphType::Delegate);
		MergeFunctionGraphs(Base, Left, Right, MergedBlueprint, EGraphType::Ubergraph);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create a new blueprint."));
	}
}


TMap<FName, UBlueprintMergeLibrary::FPropertyData> UBlueprintMergeLibrary::BuildPropertyMap(UObject* Target, EBuildPropertyMapOption Option)
{
	TMap<FName, FPropertyData> PropertyMap;

	for (TPropertyValueIterator<FProperty> PropertyIterator(Target->GetClass(), Target); PropertyIterator; ++PropertyIterator)
	{
		// Transient プロパティはスキップ
		if (PropertyIterator->Key->HasAnyPropertyFlags(CPF_Transient) ||
			PropertyIterator->Key->HasAnyPropertyFlags(CPF_EditConst))
		{
			continue;
		}


		FString PropertyPath;

		bool bIsArrayElement = false;
		if (PropertyIterator->Key->Owner.IsA<FArrayProperty>())
		{
			bIsArrayElement = true;
		}

		if ((Option != EBuildPropertyMapOption::IncludeCompositeType) &&
			(	PropertyIterator->Key->IsA<FArrayProperty>() ||
				PropertyIterator->Key->IsA<FMapProperty>() ||
				PropertyIterator->Key->IsA<FSetProperty>() ||
				PropertyIterator->Key->IsA<FStructProperty>()
				))
		{
			continue;
		}

		// PropertyA.PropertyB.PropertyC... という形式に変換する
		TArray<const FProperty*> PropertyChain;
		PropertyIterator.GetPropertyChain(PropertyChain);
		Algo::Reverse(PropertyChain);
		for (const FProperty* Property : PropertyChain)
		{
			FString AppendName = Property->GetName();
			if (Property == PropertyIterator->Key)
			{
				if (bIsArrayElement)
				{
					AppendName = FString::Printf(TEXT("[%d]"), Property->GetIndexInOwner());
				}
			}
			PropertyPath += ((PropertyPath.IsEmpty() || bIsArrayElement) ? "" : ".") + AppendName;
		}

		PropertyMap.Emplace(FName(*PropertyPath), FPropertyData(PropertyIterator.Key(), PropertyIterator.Value()));
	}

	return PropertyMap;
}

TMap<FName, USCS_Node*> UBlueprintMergeLibrary::BuildSCSNodeMap(UBlueprintGeneratedClass* BPGC)
{
	TMap<FName, USCS_Node*> SCSNodeMap;
	TArray<USCS_Node*> SCSNodes = BPGC->SimpleConstructionScript->GetRootNodes();
	for (USCS_Node* Node : SCSNodes)
	{
		if (Node->ComponentClass->IsChildOf(UActorComponent::StaticClass()))
		{
			FString Path = Node->GetVariableName().ToString();
			SCSNodeMap.Emplace(FName(*Path), Node);
			BuildSCSNodeMapRecursive(Node, Path, SCSNodeMap);
		}
	}

	return SCSNodeMap;
}

void UBlueprintMergeLibrary::BuildSCSNodeMapRecursive(USCS_Node* Node, const FString& Path, TMap<FName, USCS_Node*>& InOutMap)
{
	for (USCS_Node* ChildNode : Node->GetChildNodes())
	{
		InOutMap.Emplace(FName(*(Path + TEXT('.') + ChildNode->GetVariableName().ToString())), ChildNode);
		BuildSCSNodeMapRecursive(ChildNode, Path, InOutMap);
	}
}

TMap<FName, UEdGraph*> UBlueprintMergeLibrary::BuildGraphMap(UBlueprint* Blueprint, EGraphType Type)
{
	TMap<FName, UEdGraph*> GraphNodeMap;

	TArray<UEdGraph*> RootGraphs;
	if (Type == EGraphType::None)
	{
		return GraphNodeMap;
	}

	if (Type == EGraphType::Function)
	{
		RootGraphs = Blueprint->FunctionGraphs;
	}
	else if (Type == EGraphType::Event)
	{
		RootGraphs = Blueprint->EventGraphs;
	}
	else if (Type == EGraphType::Macro)
	{
		RootGraphs = Blueprint->MacroGraphs;
	}
	else if (Type == EGraphType::Delegate)
	{
		RootGraphs = Blueprint->DelegateSignatureGraphs;
	}
	else if (Type == EGraphType::Ubergraph)
	{
		RootGraphs = Blueprint->UbergraphPages;
	}

	for (UEdGraph* Graph : RootGraphs)
	{
		FString Path = Graph->GetName();
		GraphNodeMap.Emplace(FName(*Path), Graph);

		TArray<UEdGraph*> ChildGraphs;
		Graph->GetAllChildrenGraphs(ChildGraphs);
		for (UEdGraph* ChildGraph : ChildGraphs)
		{
			BuildGraphMapRecursive(ChildGraph, Path, GraphNodeMap);
		}
	}
	return GraphNodeMap;
}

void UBlueprintMergeLibrary::BuildGraphMapRecursive(UEdGraph* Graph, const FString& Path, TMap<FName, UEdGraph*>& InOutMap)
{
	InOutMap.Emplace(FName(*(Path + TEXT('.') + Graph->GetName())), Graph);

	TArray<UEdGraph*> ChildGraphs;
	Graph->GetAllChildrenGraphs(ChildGraphs);
	for (UEdGraph* ChildGraph : ChildGraphs)
	{
		BuildGraphMapRecursive(ChildGraph, Path, InOutMap);
	}
}

TMap<FName, UEdGraphNode*> UBlueprintMergeLibrary::BuildGraphNodesMap(UEdGraph* Graph)
{
	TMap<FName, UEdGraphNode*> GraphNodeMap;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		FString Path = Node->GetName();
		GraphNodeMap.Emplace(FName(*Path), Node);
	}
	return GraphNodeMap;
}

TMap<FName, UEdGraphPin*> UBlueprintMergeLibrary::BuildGraphPinsMap(UEdGraphNode* Node)
{
	TMap<FName, UEdGraphPin*> GraphPinMap;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		FString Path = Pin->GetName();
		GraphPinMap.Emplace(FName(*Path), Pin);
	}
	return GraphPinMap;
}

FString UBlueprintMergeLibrary::GetObjectPath(UObject* Root, UObject* Object, bool bRequiredRootName)
{
	if (!Root || !Object)
	{
		return FString();
	}

	UObject* Current = Object;
	FString Path = Current->GetName();
	while (true)
	{
		if (Current->GetOuter())
		{
			Current = Current->GetOuter();
		}
		else
		{
			return TEXT("");
		}

		if (Current == Root)
		{
			break;
		}
		Path = Current->GetName() + TEXT(".") + Path;	
	}

	if (bRequiredRootName)
	{
		Path = Root->GetName() + TEXT(".") + Path;
	}
	return Path;
}

void UBlueprintMergeLibrary::MergeBlueprintMemberVariables(UBlueprint* Base, const TMap<FName, FPropertyData>& BasePropertyMap, UBlueprint* Left, const TMap<FName, FPropertyData>& LeftPropertyMap, UBlueprint* Right, const TMap<FName, FPropertyData>& RightPropertyMap, const TMap<FName, FDiffData>& DiffPropertyMap, const TSet<FName>& UnionPropertyKeys, const TMap<FName, FPropertyData>& MergedPropertyMap, UBlueprint* InOutMergedBlueprint)
{
	if (!Base || !Left || !Right || !InOutMergedBlueprint)
	{
		return;
	}

	for (const TPair<FName, FDiffData>& Pair : DiffPropertyMap)
	{
		FName PropertyPath = Pair.Key;
		const FDiffData& DiffPropertyData = Pair.Value;

		const FPropertyData* LeftPropertyData = LeftPropertyMap.Find(PropertyPath);
		const FPropertyData* RightPropertyData = RightPropertyMap.Find(PropertyPath);
		const FPropertyData* MergedPropertyData = MergedPropertyMap.Find(PropertyPath);

		if (DiffPropertyData.IsNoDifference())
		{
			// 差分がない場合はスキップ
			continue;
		}

		if (DiffPropertyData.GetDiffType() != EDiffType::Remove &&
			(DiffPropertyData.IsLeftUpdate() && DiffPropertyData.IsRightUpdate()))
		{
			// コンフリクト
			continue;
		}

		UBlueprint* UpdatedBlueprint = DiffPropertyData.IsLeftUpdate() ? Left : Right;
		const FPropertyData* UpdatedPropertyData = DiffPropertyData.IsLeftUpdate() ? LeftPropertyData : RightPropertyData;

		if (!MergedPropertyData)
		{
			if (DiffPropertyData.GetDiffType() == EDiffType::Add)
			{
				int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(UpdatedBlueprint, PropertyPath);
				// プロパティが見つからない場合は何もしない
				if (VariableIndex < 0)
				{
					continue;
				}

				FBPVariableDescription& VariableDesc = UpdatedBlueprint->NewVariables[VariableIndex];
				FBlueprintEditorUtils::AddMemberVariable(InOutMergedBlueprint, PropertyPath, VariableDesc.VarType, TEXT(""));
			}
		}
		else if (DiffPropertyData.GetDiffType() == EDiffType::Remove)
		{
			FBlueprintEditorUtils::RemoveMemberVariable(InOutMergedBlueprint, PropertyPath);
		}
	}
}

void UBlueprintMergeLibrary::MergeObjectProperties(UObject* Base, UObject* Left, UObject* Right, UObject* InOutMergedObject)
{
	TMap<FName, FPropertyData> BasePropertyMap = BuildPropertyMap(Base);
	TMap<FName, FPropertyData> LeftPropertyMap = BuildPropertyMap(Left);
	TMap<FName, FPropertyData> RightPropertyMap = BuildPropertyMap(Right);
	TMap<FName, FPropertyData> MergedPropertyMap = BuildPropertyMap(InOutMergedObject);

	TMap<FName, FDiffData> DiffPropertyMap;

	// マップの要素を結合する

	// キーを統合する
	TSet<FName> UnionPropertyKeys;
	{
		TSet<FName> Keys;
		BasePropertyMap.GetKeys(Keys);
		UnionPropertyKeys = UnionPropertyKeys.Union(Keys);

		LeftPropertyMap.GetKeys(Keys);
		UnionPropertyKeys = UnionPropertyKeys.Union(Keys);

		RightPropertyMap.GetKeys(Keys);
		UnionPropertyKeys = UnionPropertyKeys.Union(Keys);
	}

	for (const FName& PropertyPath : UnionPropertyKeys)
	{
		const FPropertyData* BasePropertyData = BasePropertyMap.Find(PropertyPath);
		const FPropertyData* LeftPropertyData = LeftPropertyMap.Find(PropertyPath);
		const FPropertyData* RightPropertyData = RightPropertyMap.Find(PropertyPath);

		EDiffType DiffType = EDiffType::None;
		bool bIsLeftUpdate = false;
		bool bIsRightUpdate = false;

		if (BasePropertyData)
		{
			if (LeftPropertyData && RightPropertyData)
			{
				// 差分があるかチェック
				if (!BasePropertyData->Property->Identical(BasePropertyData->Container, LeftPropertyData->Container))
				{
					DiffType = EDiffType::Modify;
					bIsLeftUpdate = true;
				}
				if (!BasePropertyData->Property->Identical(BasePropertyData->Container, RightPropertyData->Container))
				{
					DiffType = EDiffType::Modify;
					bIsRightUpdate = true;
				}

				if (bIsLeftUpdate && bIsRightUpdate)
				{
					// 両方の変更が等しい場合、片方の変更を反映すればいいので、片方のフラグを下す
					if (LeftPropertyData->Property->Identical(LeftPropertyData->Container, RightPropertyData->Container))
					{
						bIsRightUpdate = false;
					}
				}
			}

			if (!LeftPropertyData || !RightPropertyData)
			{
				DiffType = EDiffType::Remove;
				bIsLeftUpdate = !!LeftPropertyData;
				bIsRightUpdate = !!RightPropertyData;
			}
		}
		else
		{
			if (LeftPropertyData || RightPropertyData)
			{
				DiffType = EDiffType::Add;
				bIsLeftUpdate = !!LeftPropertyData;
				bIsRightUpdate = !!RightPropertyData;
			}
		}

		if (!bIsLeftUpdate && !bIsRightUpdate)
		{
			// 差分がない場合はスキップ
			continue;
		}

		DiffPropertyMap.Emplace(PropertyPath, FDiffData(PropertyPath, DiffType, bIsLeftUpdate, bIsRightUpdate));
	}

	for (TPair<FName, FDiffData>& Pair : DiffPropertyMap)
	{
		FName PropertyPath = Pair.Key;
		const FDiffData& DiffPropertyData = Pair.Value;

		const FPropertyData* LeftPropertyData = LeftPropertyMap.Find(PropertyPath);
		const FPropertyData* RightPropertyData = RightPropertyMap.Find(PropertyPath);
		const FPropertyData* MergedPropertyData = MergedPropertyMap.Find(PropertyPath);

		if (DiffPropertyData.IsNoDifference())
		{
			// 差分がない場合はスキップ
			continue;
		}

		if (DiffPropertyData.IsLeftUpdate() && DiffPropertyData.IsRightUpdate())
		{
			// コンフリクト
			UE_LOG(LogTemp, Log, TEXT("Conflict!! PropertyName[%s]"), *PropertyPath.ToString());
			continue;
		}

		if (MergedPropertyData)
		{
			if (DiffPropertyData.IsLeftUpdate())
			{
				MergedPropertyData->Property->CopyCompleteValue(const_cast<void*>(MergedPropertyData->Container), LeftPropertyData->Container);
			}
			else if (DiffPropertyData.IsRightUpdate())
			{
				MergedPropertyData->Property->CopyCompleteValue(const_cast<void*>(MergedPropertyData->Container), RightPropertyData->Container);
			}
		}
	}
}

void UBlueprintMergeLibrary::MergeComponentProperties(FPropertyData& BaseComponentProperty, FPropertyData& LeftComponentProperty, FPropertyData& RightComponentProperty, FPropertyData& InOutMergedComponentProperty)
{
	const FObjectProperty* BaseObjectProperty = CastField<FObjectProperty>(BaseComponentProperty.Property);
	const FObjectProperty* LeftObjectProperty = CastField<FObjectProperty>(LeftComponentProperty.Property);
	const FObjectProperty* RightObjectProperty = CastField<FObjectProperty>(RightComponentProperty.Property);
	const FObjectProperty* MergedObjectProperty = CastField<FObjectProperty>(InOutMergedComponentProperty.Property);
	if (!FBlueprintEditorUtils::IsSCSComponentProperty(const_cast<FObjectProperty*>(BaseObjectProperty)) ||
		!FBlueprintEditorUtils::IsSCSComponentProperty(const_cast<FObjectProperty*>(LeftObjectProperty)) ||
		!FBlueprintEditorUtils::IsSCSComponentProperty(const_cast<FObjectProperty*>(RightObjectProperty)) ||
		!FBlueprintEditorUtils::IsSCSComponentProperty(const_cast<FObjectProperty*>(MergedObjectProperty)))
	{
		return;
	}

	UActorComponent* BaseComponent = Cast<UActorComponent>(BaseObjectProperty->GetObjectPropertyValue(BaseComponentProperty.Container));
	UActorComponent* LeftComponent = Cast<UActorComponent>(LeftObjectProperty->GetObjectPropertyValue(LeftComponentProperty.Container));
	UActorComponent* RightComponent = Cast<UActorComponent>(RightObjectProperty->GetObjectPropertyValue(RightComponentProperty.Container));
	UActorComponent* MergedComponent = Cast<UActorComponent>(MergedObjectProperty->GetObjectPropertyValue(InOutMergedComponentProperty.Container));
	
	if (BaseComponent && LeftComponent && RightComponent && MergedComponent)
	{
		// プロパティをマージ
		MergeObjectProperties(BaseComponent, LeftComponent, RightComponent, MergedComponent);
	}
}

void UBlueprintMergeLibrary::MergeBlueprintComponents(UBlueprint* Base, UBlueprint* Left, UBlueprint* Right, UBlueprint* InOutMergedBlueprint)
{
	if (!Base || !Left || !Right || !InOutMergedBlueprint)
	{
		return;
	}

	auto ExistSameComponent = [](const TArray<UActorComponent*>& Components, UActorComponent* Component) -> bool
	{
		return Components.ContainsByPredicate([Component](UActorComponent* InComponent)
		{
			return InComponent->GetFName() == Component->GetFName();
		});
	};

	auto GetComponents = [](UBlueprint* ComponentOwner) ->  TArray<UActorComponent*>
	{
		if (AActor* Actor = Cast<AActor>(ComponentOwner->GeneratedClass->GetDefaultObject()))
		{
			return Actor->BlueprintCreatedComponents;
		}
		return {};
	};
	TArray<UActorComponent*> BaseComponents = GetComponents(Base);

	auto GetNewComponents = [&ExistSameComponent](const TArray<UActorComponent*>& BaseComponents, const TArray<UActorComponent*>& OtherComponents) ->  TArray<UActorComponent*>
	{
		TArray<UActorComponent*> NewComponents;
		for (UActorComponent* Component : OtherComponents)
		{
			if (!ExistSameComponent(BaseComponents, Component))
			{
				NewComponents.Add(Component);
			}
		}
		return NewComponents;
	};

	TArray<UActorComponent*> LeftComponents;
	TArray<UActorComponent*> RightComponents;
	LeftComponents = GetComponents(Left);
	RightComponents = GetComponents(Right);

	TMap<FName, USCS_Node*> BaseSCSNodeMap = BuildSCSNodeMap(Cast<UBlueprintGeneratedClass>(Base->GeneratedClass));
	TMap<FName, USCS_Node*> LeftSCSNodeMap = BuildSCSNodeMap(Cast<UBlueprintGeneratedClass>(Left->GeneratedClass));
	TMap<FName, USCS_Node*> RightSCSNodeMap = BuildSCSNodeMap(Cast<UBlueprintGeneratedClass>(Right->GeneratedClass));
	TMap<FName, USCS_Node*> MergedSCSNodeMap = BuildSCSNodeMap(Cast<UBlueprintGeneratedClass>(InOutMergedBlueprint->GeneratedClass));

	// キーを統合する
	TSet<FName> UnionKeys;
	{
		TSet<FName> Keys;
		BaseSCSNodeMap.GetKeys(Keys);
		UnionKeys = UnionKeys.Union(Keys);

		LeftSCSNodeMap.GetKeys(Keys);
		UnionKeys = UnionKeys.Union(Keys);

		RightSCSNodeMap.GetKeys(Keys);
		UnionKeys = UnionKeys.Union(Keys);
	}

	TMap<FName, FDiffData> DiffMap;
	for (const FName& Path : UnionKeys)
	{
		const USCS_Node* BaseNode = BaseSCSNodeMap.FindRef(Path);
		const USCS_Node* LeftNode = LeftSCSNodeMap.FindRef(Path);
		const USCS_Node* RightNode = RightSCSNodeMap.FindRef(Path);
		const USCS_Node* MergedNode = MergedSCSNodeMap.FindRef(Path);

		EDiffType DiffType = EDiffType::None;
		bool bIsLeftUpdate = false;
		bool bIsRightUpdate = false;

		if (BaseNode)
		{
			if (LeftNode && RightNode)
			{
				MergeObjectProperties(BaseNode->ComponentTemplate, LeftNode->ComponentTemplate, RightNode->ComponentTemplate, MergedNode->ComponentTemplate);
			}
			else
			{
				DiffType = EDiffType::Remove;
				bIsLeftUpdate = !!LeftNode;
				bIsRightUpdate = !!RightNode;
			}
		}
		else
		{
			if (LeftNode || RightNode)
			{
				DiffType = EDiffType::Add;
				bIsLeftUpdate = !!LeftNode;
				bIsRightUpdate = !!RightNode;
			}
		}

		if (!bIsLeftUpdate && !bIsRightUpdate)
		{
			// 差分がない場合はスキップ
			continue;
		}

		DiffMap.Emplace(Path, FDiffData(Path, DiffType, bIsLeftUpdate, bIsRightUpdate));
	}

	for (TPair<FName, FDiffData>& Pair : DiffMap)
	{
		FName Path = Pair.Key;
		const FDiffData& DiffData = Pair.Value;

		USCS_Node* LeftNode = LeftSCSNodeMap.FindRef(Path);
		USCS_Node* RightNode = RightSCSNodeMap.FindRef(Path);
		USCS_Node* MergedNode = MergedSCSNodeMap.FindRef(Path);

		if (DiffData.IsNoDifference())
		{
			// 差分がない場合はスキップ
			continue;
		}

		if (DiffData.IsLeftUpdate() && DiffData.IsRightUpdate())
		{
			// コンフリクト
			UE_LOG(LogTemp, Log, TEXT("Conflict!! ComponentName[%s]"), *Path.ToString());
			continue;
		}

		if (!MergedNode)
		{
			if (DiffData.GetDiffType() == EDiffType::Add)
			{
				// ノードが見つからない場合は何もしない
				if (!LeftNode && !RightNode)
				{
					continue;
				}

				USCS_Node* UpdateNode = LeftNode ? LeftNode : RightNode;;
				USCS_Node* NewNode = DuplicateObject(UpdateNode, InOutMergedBlueprint->SimpleConstructionScript);
				NewNode->ComponentTemplate = DuplicateObject(UpdateNode->ComponentTemplate, InOutMergedBlueprint->SimpleConstructionScript->GetOwnerClass());
				NewNode->ChildNodes.Empty();

				if (USCS_Node* ParentNode = UpdateNode->GetSCS()->FindParentNode(UpdateNode))
				{
					InOutMergedBlueprint->SimpleConstructionScript->FindSCSNode(ParentNode->GetVariableName())->AddChildNode(NewNode);
				}
			}
		}
		else if (DiffData.GetDiffType() == EDiffType::Remove)
		{
			// Removeは処理しない
		}
	}

	TArray<UActorComponent*> NewComponents = GetNewComponents(BaseComponents, LeftComponents);
	NewComponents.Append(GetNewComponents(BaseComponents, RightComponents));
	FKismetEditorUtilities::CompileBlueprint(InOutMergedBlueprint);
}

void UBlueprintMergeLibrary::MergeFunctionGraphs(UBlueprint* Base, UBlueprint* Left, UBlueprint* Right, UBlueprint* InOutMergedBlueprint, EGraphType Type)
{
	if (!Base || !Left || !Right || !InOutMergedBlueprint)
	{
		return;
	}

	TMap<FName, UEdGraph*> BaseGraphMap = BuildGraphMap(Base, Type);
	TMap<FName, UEdGraph*> LeftGraphMap = BuildGraphMap(Left, Type);
	TMap<FName, UEdGraph*> RightGraphMap = BuildGraphMap(Right, Type);
	TMap<FName, UEdGraph*> MergedGraphMap = BuildGraphMap(InOutMergedBlueprint, Type);

	// キーを統合する
	TSet<FName> UnionKeys;
	{
		TSet<FName> Keys;
		BaseGraphMap.GetKeys(Keys);
		UnionKeys = UnionKeys.Union(Keys);

		LeftGraphMap.GetKeys(Keys);
		UnionKeys = UnionKeys.Union(Keys);

		RightGraphMap.GetKeys(Keys);
		UnionKeys = UnionKeys.Union(Keys);
	}

	TMap<FName, FDiffData> DiffMap;
	for (const FName& Path : UnionKeys)
	{
		UEdGraph* BaseGraph = BaseGraphMap.FindRef(Path);
		UEdGraph* LeftGraph = LeftGraphMap.FindRef(Path);
		UEdGraph* RightGraph = RightGraphMap.FindRef(Path);
		const UEdGraph* MergedGraph = MergedGraphMap.FindRef(Path);

		EDiffType DiffType = EDiffType::None;
		bool bIsLeftUpdate = false;
		bool bIsRightUpdate = false;

		if (BaseGraph)
		{
			TArray<FName> LeftDiffProperties;
			TArray<FName> RightDiffProperties;
			if (LeftGraph && !IdenticalGraphs(BaseGraph, LeftGraph, LeftDiffProperties))
			{
				DiffType = EDiffType::Modify;
				bIsLeftUpdate = true;
			}

			if (RightGraph && !IdenticalGraphs(BaseGraph, RightGraph, RightDiffProperties))
			{
				DiffType = EDiffType::Modify;
				bIsRightUpdate = true;
			}

			if (!LeftGraph || !RightGraph)
			{
				DiffType = EDiffType::Remove;
				bIsLeftUpdate |= !LeftGraph;
				bIsRightUpdate |= !RightGraph;
			}

			if (bIsLeftUpdate && bIsRightUpdate)
			{
				TArray<FName> DiffProperties;

				// 両方の変更が等しい場合、片方の変更を反映すればいいので、片方のフラグを下す
				if (IdenticalGraphs(LeftGraph, RightGraph, DiffProperties))
				{
					bIsRightUpdate = false;
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("Conflict!! LeftGraph[%s] RightGraph[%s]"), LeftGraph ? *GetObjectPath(LeftGraph->GetOutermostObject(), LeftGraph, true) : TEXT("nullptr"), RightGraph ? *GetObjectPath(RightGraph->GetOutermostObject(), RightGraph, true) : TEXT("nullptr"));
					for (const FName& Property : DiffProperties)
					{
						UE_LOG(LogTemp, Log, TEXT("Diff Property: %s"), *Property.ToString());
					}
				}
			}
		}
		else
		{
			if (LeftGraph || RightGraph)
			{
				DiffType = EDiffType::Add;
				bIsLeftUpdate = !!LeftGraph;
				bIsRightUpdate = !!RightGraph;
			}
		}

		if (!bIsLeftUpdate && !bIsRightUpdate)
		{
			// 差分がない場合はスキップ
			continue;
		}

		DiffMap.Emplace(Path, FDiffData(Path, DiffType, bIsLeftUpdate, bIsRightUpdate));
	}

	for (TPair<FName, FDiffData>& Pair : DiffMap)
	{
		FName Path = Pair.Key;
		const FDiffData& DiffData = Pair.Value;

		UEdGraph* LeftGraph = LeftGraphMap.FindRef(Path);
		UEdGraph* RightGraph = RightGraphMap.FindRef(Path);
		UEdGraph* MergedGraph = MergedGraphMap.FindRef(Path);

		if (DiffData.IsNoDifference())
		{
			// 差分がない場合はスキップ
			continue;
		}

		if (DiffData.IsLeftUpdate() && DiffData.IsRightUpdate())
		{
			UE_LOG(LogTemp, Log, TEXT("Conflict!! LeftGraph[%s] RightGraph[%s]"), LeftGraph ? *LeftGraph->GetName() : TEXT("nullptr"), RightGraph ? *RightGraph->GetName() : TEXT("nullptr"));
			// コンフリクト
			continue;
		}

		if (!MergedGraph)
		{
			if (DiffData.GetDiffType() == EDiffType::Add)
			{
				// ノードが見つからない場合は何もしない
				if (!LeftGraph && !RightGraph)
				{
					continue;
				}

				UEdGraph* UpdateGraph = LeftGraph ? LeftGraph : RightGraph;
				UEdGraph* NewGraph = DuplicateObject(UpdateGraph, InOutMergedBlueprint);
				InOutMergedBlueprint->FunctionGraphs.Add(NewGraph);
			}
		}
		else if (DiffData.GetDiffType() == EDiffType::Remove)
		{
			FBlueprintEditorUtils::RemoveGraph(InOutMergedBlueprint, MergedGraph);
		}
		else if (DiffData.GetDiffType() == EDiffType::Modify)
		{
			FBlueprintEditorUtils::RemoveGraph(InOutMergedBlueprint, MergedGraph);
			if (DiffData.IsLeftUpdate())
			{
				UEdGraph* NewGraph = DuplicateObject(LeftGraph, InOutMergedBlueprint);
				AddGraphToBlueprint(InOutMergedBlueprint, NewGraph, Type);
			}
			else if (DiffData.IsRightUpdate())
			{
				UEdGraph* NewGraph = DuplicateObject(RightGraph, InOutMergedBlueprint);
				AddGraphToBlueprint(InOutMergedBlueprint, NewGraph, Type);
			}
		}
	}
}

bool UBlueprintMergeLibrary::IdenticalGraphs(UEdGraph* LeftGraph, UEdGraph* RightGraph, TArray<FName>& OutConflictProperties)
{
	if (!LeftGraph && !RightGraph)
	{
		return true;
	}

	if (!LeftGraph || !RightGraph)
	{
		return false;
	}

	OutConflictProperties.Empty();

	TMap<FName, FPropertyData> LeftPropertyMap = BuildPropertyMap(LeftGraph);
	TMap<FName, FPropertyData> RightPropertyMap = BuildPropertyMap(RightGraph);

	// キーを統合する
	{
		TSet<FName> UnionKeys;
		{
			TSet<FName> Keys;
			LeftPropertyMap.GetKeys(Keys);
			UnionKeys = UnionKeys.Union(Keys);

			RightPropertyMap.GetKeys(Keys);
			UnionKeys = UnionKeys.Union(Keys);
		}

		// 差分があるかチェック
		for (const FName& PropertyPath : UnionKeys)
		{
			const FPropertyData* LeftPropertyData = LeftPropertyMap.Find(PropertyPath);
			const FPropertyData* RightPropertyData = RightPropertyMap.Find(PropertyPath);

			if (PropertyPath.ToString().ToUpper().Contains(TEXT("GUID")))
			{
				// GUIDは自動生成されるため比較しない
				continue;
			}

			if (LeftPropertyData && RightPropertyData)
			{
				if (!IdenticalProperties(LeftGraph->GetOutermostObject(), *LeftPropertyData, RightGraph->GetOutermostObject(), *RightPropertyData))
				{
					OutConflictProperties.Emplace(PropertyPath);
					return false;
				}
			}
			else if (!LeftPropertyData || !RightPropertyData)
			{
				return false;
			}
		}
	}

	// グラフが等しかったので、ノードを比較
	TMap<FName, UEdGraphNode*> LeftNodeMap = BuildGraphNodesMap(LeftGraph);
	TMap<FName, UEdGraphNode*> RightNodeMap = BuildGraphNodesMap(RightGraph);
	{
		TSet<FName> UnionKeys;
		{
			TSet<FName> Keys;
			LeftNodeMap.GetKeys(Keys);
			UnionKeys = UnionKeys.Union(Keys);

			RightNodeMap.GetKeys(Keys);
			UnionKeys = UnionKeys.Union(Keys);
		}

		for (const FName& NodePath : UnionKeys)
		{
			UEdGraphNode* LeftNode = LeftNodeMap.FindRef(NodePath);
			UEdGraphNode* RightNode = RightNodeMap.FindRef(NodePath);

			if (LeftNode && RightNode)
			{
				if (!IdenticalNodes(LeftGraph, LeftNode, RightGraph,  RightNode))
				{
					return false;
				}
			}
			else if (!LeftNode || !RightNode)
			{
				return false;
			}
		}
	}

	return true;
}

bool UBlueprintMergeLibrary::IdenticalNodes(UEdGraph* LeftGraph, UEdGraphNode* LeftNode, UEdGraph* RightGraph, UEdGraphNode* RightNode)
{
	if (!LeftNode || !RightNode)
	{
		return false;
	}

	TMap<FName, FPropertyData> LeftPropertyMap = BuildPropertyMap(LeftNode);
	TMap<FName, FPropertyData> RightPropertyMap = BuildPropertyMap(RightNode);

	// キーを統合する
	{
		TSet<FName> UnionKeys;
		{
			TSet<FName> Keys;
			LeftPropertyMap.GetKeys(Keys);
			UnionKeys = UnionKeys.Union(Keys);

			RightPropertyMap.GetKeys(Keys);
			UnionKeys = UnionKeys.Union(Keys);
		}

		// ノードプロパティを比較
		for (const FName& PropertyPath : UnionKeys)
		{
			const FPropertyData* LeftPropertyData = LeftPropertyMap.Find(PropertyPath);
			const FPropertyData* RightPropertyData = RightPropertyMap.Find(PropertyPath);

			if (LeftPropertyData && RightPropertyData)
			{
				// 除外するプロパティ
				if (PropertyPath.ToString().ToUpper().Contains(TEXT("GUID")))
				{
					// GUIDは自動生成されるため比較しない
					continue;
				}

				if (!LeftPropertyData->Property->Identical(LeftPropertyData->Container, RightPropertyData->Container))
				{
					OutputPropertyValues(*LeftPropertyData, FString::Printf(TEXT("Context[%s] Left"), *GetObjectPath(LeftGraph->GetOutermostObject(), LeftGraph, true)));
					OutputPropertyValues(*RightPropertyData, FString::Printf(TEXT("Context[%s] Right"), *GetObjectPath(RightGraph->GetOutermostObject(), RightGraph, true)));
					return false;
				}
			}
			else if (!LeftPropertyData || !RightPropertyData)
			{
				return false;
			}
		}
	}

	// ノードが等しかったので、ピンを比較
	TMap<FName, UEdGraphPin*> LeftPinsMap = BuildGraphPinsMap(LeftNode);
	TMap<FName, UEdGraphPin*> RightPinsMap = BuildGraphPinsMap(RightNode);
	{
		// キーを統合する
		TSet<FName> UnionKeys;
		{
			TSet<FName> Keys;
			LeftPinsMap.GetKeys(Keys);
			UnionKeys = UnionKeys.Union(Keys);

			RightPinsMap.GetKeys(Keys);
			UnionKeys = UnionKeys.Union(Keys);

		}

		// 差分があるかチェック
		for (const FName& PinName : UnionKeys)
		{
			UEdGraphPin* LeftPin = LeftPinsMap.FindRef(PinName);
			UEdGraphPin* RightPin = RightPinsMap.FindRef(PinName);

			if (LeftPin && RightPin)
			{
				if (!IdenticalPins(LeftPin, RightPin))
				{
					return false;
				}
			}
			else if (!LeftPin || !RightPin)
			{
				return false;
			}
		}
	}
	return true;
}

bool UBlueprintMergeLibrary::IdenticalPins(UEdGraphPin* LeftPin, UEdGraphPin* RightPin)
{
	if (!LeftPin || !RightPin)
	{
		return false;
	}

	return true;
}

bool UBlueprintMergeLibrary::IdenticalProperties(UObject* LeftRootObject, const FPropertyData& Left, UObject* RightRootObject, const FPropertyData& Right)
{
	if (Left.Property->GetClass() != Right.Property->GetClass())
	{
		return false;
	}

	if (Left.Property->IsA<FObjectProperty>())
	{
		const FObjectProperty* LeftObjectProperty = CastField<FObjectProperty>(Left.Property);
		const FObjectProperty* RightObjectProperty = CastField<FObjectProperty>(Right.Property);

		UObject* LeftObject = LeftObjectProperty->GetObjectPropertyValue(Left.Container);
		UObject* RightObject = RightObjectProperty->GetObjectPropertyValue(Right.Container);

		UObject* LeftOuterMost = LeftObject->GetOutermostObject();
		UObject* RightOuterMost = RightObject->GetOutermostObject();

		if (LeftObject && RightObject)
		{
			if ((LeftOuterMost == LeftRootObject) && (RightOuterMost == RightRootObject))
			{
				FString LeftObjectPath = GetObjectPath(LeftRootObject, LeftObject);
				FString RightObjectPath = GetObjectPath(RightRootObject, RightObject);
				return LeftObjectPath == RightObjectPath;
			}
			return LeftObject == RightObject;
		}
		else if (!LeftObject || !RightObject)
		{
			return false;
		}
	}
	else if (Left.Property->IsA<FArrayProperty>())
	{
		const FArrayProperty* LeftArrayProperty = CastField<FArrayProperty>(Left.Property);
		const FArrayProperty* RightArrayProperty = CastField<FArrayProperty>(Right.Property);

		FScriptArrayHelper LeftArrayHelper(LeftArrayProperty, Left.Container);
		FScriptArrayHelper RightArrayHelper(RightArrayProperty, Right.Container);

		int32 LeftNum = LeftArrayHelper.Num();
		int32 RightNum = RightArrayHelper.Num();

		for (int32 Index = 0; Index < LeftArrayHelper.Num(); ++Index)
		{
			if (const FObjectProperty* LeftObjectProperty = CastField<FObjectProperty>(LeftArrayProperty->Inner))
			{
				UObject* LeftObject = LeftObjectProperty->GetObjectPropertyValue(LeftArrayHelper.GetRawPtr(Index));
				UObject* LeftOuterMost = LeftObject->GetOutermostObject();
				if (LeftOuterMost == LeftRootObject)
				{
					FString LeftObjectPath = GetObjectPath(LeftRootObject, LeftObject);
					UE_LOG(LogTemp, Log, TEXT("Index[%d] Name[%s]"), Index, *LeftObjectPath);
				}
			}
		}

		for (int32 Index = 0; Index < RightArrayHelper.Num(); ++Index)
		{
			if (const FObjectProperty* RightObjectProperty = CastField<FObjectProperty>(RightArrayProperty->Inner))
			{
				UObject* RightObject = RightObjectProperty->GetObjectPropertyValue(RightArrayHelper.GetRawPtr(Index));
				UObject* RightOuterMost = RightObject->GetOutermostObject();
				if (RightOuterMost == RightRootObject)
				{
					FString RightObjectPath = GetObjectPath(RightRootObject, RightObject);
					UE_LOG(LogTemp, Log, TEXT("Index[%d] Name[%s]"), Index, *RightObjectPath);
				}
			}
		}

		if (LeftNum != RightNum)
		{
			return false;
		}

		for (int32 Index = 0; Index < LeftArrayHelper.Num(); ++Index)
		{
			if (!IdenticalProperties(LeftRootObject, FPropertyData(LeftArrayProperty->Inner, LeftArrayHelper.GetRawPtr(Index)), RightRootObject, FPropertyData(RightArrayProperty->Inner, RightArrayHelper.GetRawPtr(Index))))
			{
				return false;
			}
		}
	}
	else
	{
		if (!Left.Property->Identical(Left.Container, Right.Container))
		{
			OutputPropertyValues(Left, TEXT("Diff Left"));
			OutputPropertyValues(Right, TEXT("Diff Right"));
			return false;
		}
	}
	return true;
}

void UBlueprintMergeLibrary::OutputPropertyValues(const FPropertyData& PropertyData, const FString& ContextString)
{
	// プロパティを文字列化
	FString PropertyString;
	PropertyData.Property->ExportTextItem_Direct(PropertyString, PropertyData.Container, PropertyData.Container, nullptr, PPF_None);
	UE_LOG(LogTemp, Log, TEXT("%s : Property[%s] Value[%s]"), *ContextString, *PropertyData.Property->GetName(), *PropertyString);
}

void UBlueprintMergeLibrary::AddGraphToBlueprint(UBlueprint* Blueprint, UEdGraph* Graph, EGraphType Type)
{
	if (!Blueprint || !Graph)
	{
		return;
	}

	switch (Type)
	{
	case EGraphType::Function:
		Blueprint->FunctionGraphs.Add(Graph);
		break;
	case EGraphType::Macro:
		Blueprint->MacroGraphs.Add(Graph);
		break;
	case EGraphType::Delegate:
		Blueprint->DelegateSignatureGraphs.Add(Graph);
		break;
	case EGraphType::Event:
		Blueprint->EventGraphs.Add(Graph);
		break;
	case EGraphType::Ubergraph:
		Blueprint->UbergraphPages.Add(Graph);
		break;
	default:
		break;
	}
}



UE_ENABLE_OPTIMIZATION
