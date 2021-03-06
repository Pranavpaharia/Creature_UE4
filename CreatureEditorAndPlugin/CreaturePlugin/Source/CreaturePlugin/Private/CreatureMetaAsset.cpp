
#include "CreaturePluginPCH.h"
#include "CreatureMetaAsset.h"
#include "CreatureCore.h"
#include "MeshBone.h"
#include "Runtime/Engine/Classes/PhysicsEngine/ConstraintInstance.h"
#include "Runtime/Engine/Classes/PhysicsEngine/PhysicsConstraintComponent.h"

// CreatureMetaData
void CreatureMetaData::buildSkinSwapIndices(
	const FString & swap_name, 
	meshRenderBoneComposition * bone_composition,
	TArray<int32>& skin_swap_indices,
	TSet<int32>& skin_swap_region_ids
)
{
	if (!skin_swaps.Contains(swap_name))
	{
		skin_swap_indices.Empty();
		return;
	}

	auto& swap_set = skin_swaps[swap_name];
	int32 total_size = 0;
	auto& regions = bone_composition->getRegions();
	skin_swap_region_ids.Empty(100);

	for (auto cur_region : regions)
	{
		if (swap_set.Contains(cur_region->getName().ToString()))
		{
			total_size += cur_region->getNumIndices();
			skin_swap_region_ids.Add(cur_region->getTagId());
		}
	}

	skin_swap_indices.SetNum(total_size);

	int32 offset = 0;
	for (auto cur_region : regions)
	{
		if (swap_set.Contains(cur_region->getName().ToString()))
		{
			std::copy(
				cur_region->getIndices(),
				cur_region->getIndices() + cur_region->getNumIndices(),
				skin_swap_indices.GetData() + offset);
			offset += cur_region->getNumIndices();
		}
	}
}

// Bend Physics
static void SetLinearLimits(
	FConstraintInstance& Constraint,
	bool bDisableCollision,
	const uint8 XLim, const uint8 YLim, const uint8 ZLim,
	const float Size,
	bool SoftLimit = true,
	const float SoftStiffness = 0,
	const float SoftDampening = 0
)
{
	//Collision
	Constraint.SetDisableCollision(bDisableCollision);

	switch (XLim)
	{
	case 0: Constraint.SetLinearXMotion(ELinearConstraintMotion::LCM_Free); break;
	case 1: Constraint.SetLinearXMotion(ELinearConstraintMotion::LCM_Limited); break;
	case 2: Constraint.SetLinearXMotion(ELinearConstraintMotion::LCM_Locked); break;
	}
	switch (YLim)
	{
	case 0: Constraint.SetLinearYMotion(ELinearConstraintMotion::LCM_Free); break;
	case 1: Constraint.SetLinearYMotion(ELinearConstraintMotion::LCM_Limited); break;
	case 2: Constraint.SetLinearYMotion(ELinearConstraintMotion::LCM_Locked); break;
	}
	switch (ZLim)
	{
	case 0: Constraint.SetLinearZMotion(ELinearConstraintMotion::LCM_Free); break;
	case 1: Constraint.SetLinearZMotion(ELinearConstraintMotion::LCM_Limited); break;
	case 2: Constraint.SetLinearZMotion(ELinearConstraintMotion::LCM_Locked); break;
	}
	//~~~~~~~~~~

	Constraint.SetLinearLimitSize(Size);

	if (SoftLimit)
	{
		Constraint.ProfileInstance.LinearLimit.bSoftConstraint = 1;
	}
	else
	{
		Constraint.ProfileInstance.LinearLimit.bSoftConstraint = 0;
	}

	Constraint.ProfileInstance.LinearLimit.Stiffness = SoftStiffness;
	Constraint.ProfileInstance.LinearLimit.Damping = SoftDampening;
}

static void SetAngularLimits(
	FConstraintInstance& Constraint,
	const uint8 S1Lim, const uint8 S2Lim, const uint8 TLim,
	const float Swing1LimitAngle,
	const float Swing2LimitAngle,
	const float TwistLimitAngle,
	bool SoftSwingLimit = true, bool SoftTwistLimit = true,
	const float SwingStiff = 0, const float SwingDamp = 0,
	const float TwistStiff = 0, const float TwistDamp = 0
)
{
	switch (S1Lim)
	{
	case 0: Constraint.SetAngularSwing1Motion(EAngularConstraintMotion::ACM_Free); break;
	case 1: Constraint.SetAngularSwing1Motion(EAngularConstraintMotion::ACM_Limited); break;
	case 2: Constraint.SetAngularSwing1Motion(EAngularConstraintMotion::ACM_Locked); break;
	}
	switch (S2Lim)
	{
	case 0: Constraint.SetAngularSwing2Motion(EAngularConstraintMotion::ACM_Free); break;
	case 1: Constraint.SetAngularSwing2Motion(EAngularConstraintMotion::ACM_Limited); break;
	case 2: Constraint.SetAngularSwing2Motion(EAngularConstraintMotion::ACM_Locked); break;
	}
	switch (TLim)
	{
	case 0: Constraint.SetAngularTwistMotion(EAngularConstraintMotion::ACM_Free); break;
	case 1: Constraint.SetAngularTwistMotion(EAngularConstraintMotion::ACM_Limited); break;
	case 2: Constraint.SetAngularTwistMotion(EAngularConstraintMotion::ACM_Locked); break;
	}
	//~~~~~~~~~~

	//Soft Lmit?
	if (SoftSwingLimit) {
		Constraint.ProfileInstance.ConeLimit.bSoftConstraint = 1;
	}
	else {
		Constraint.ProfileInstance.ConeLimit.bSoftConstraint = 0;
	}

	if (SoftTwistLimit) {
		Constraint.ProfileInstance.TwistLimit.bSoftConstraint = 1;
	}
	else {
		Constraint.ProfileInstance.TwistLimit.bSoftConstraint = 0;
	}

	//Limit Angles
	Constraint.ProfileInstance.ConeLimit.Swing1LimitDegrees = Swing1LimitAngle;
	Constraint.ProfileInstance.ConeLimit.Swing2LimitDegrees = Swing2LimitAngle;
	Constraint.ProfileInstance.TwistLimit.TwistLimitDegrees = TwistLimitAngle;

	Constraint.ProfileInstance.ConeLimit.Stiffness = SwingStiff;
	Constraint.ProfileInstance.ConeLimit.Damping = SwingDamp;
	Constraint.ProfileInstance.TwistLimit.Stiffness = TwistStiff;
	Constraint.ProfileInstance.TwistLimit.Damping = TwistDamp;
}

void CreaturePhysicsData::createPhysicsChain(
	const FTransform& base_xform, 
	USceneComponent * attach_root,
	UObject * parent,
	const TArray<meshBone *>& bones_in,
	float stiffness,
	float damping,
	const FString& anim_clip_name_in)
{
	if (bones_in.Num() < 2)
	{
		return;
	}

	anim_clip_name = anim_clip_name_in;

	auto setupBoxSettings = [](UBoxComponent * box_in, const FVector& pt_in, bool sim_physics)
	{
		box_in->SetSimulatePhysics(sim_physics);
		box_in->SetWorldLocation(pt_in);
		box_in->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		box_in->SetEnableGravity(false);
		box_in->SetAllMassScale(1.0f);
		box_in->SetWorldScale3D(FVector(1, 1, 1));
		box_in->SetAllPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		box_in->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
		box_in->SetAllPhysicsPosition(pt_in);
		box_in->RegisterComponent();
	};

	// Create bodies
	TArray<UBoxComponent *> created_bodies;
	for (int i = 0; i < bones_in.Num(); i++)
	{
		auto cur_bone = bones_in[i];
		meshBone * next_bone = nullptr;
		FVector2D cur_basis(0, 0);

		if (i < (bones_in.Num() - 1))
		{
			next_bone = bones_in[i + 1];
			auto cur_end_pt = FVector(
				cur_bone->getWorldEndPt().x,
				cur_bone->getWorldEndPt().y,
				cur_bone->getWorldEndPt().z);
			auto next_start_pt = FVector(
				next_bone->getWorldStartPt().x,
				next_bone->getWorldStartPt().y,
				next_bone->getWorldStartPt().z);

			auto next_end_pt = FVector(
				next_bone->getWorldEndPt().x,
				next_bone->getWorldEndPt().y,
				next_bone->getWorldEndPt().z);

			auto cur_dir = cur_end_pt - next_start_pt;
			auto next_dir = next_end_pt - next_start_pt;
			auto next_unit_dir = next_dir.GetSafeNormal();
			auto next_unit_norm = FVector(-next_unit_dir.Y, next_unit_dir.X, next_unit_dir.Z);

			auto cur_u = FVector::DotProduct(cur_dir, next_unit_dir);
			auto cur_v = FVector::DotProduct(cur_dir, next_unit_norm);
			cur_basis.X = cur_u;
			cur_basis.Y = cur_v;
		}

		auto new_body = NewObject<UBoxComponent>(parent);
		created_bodies.Add(new_body);

		// Flip y and z
		auto bone_start_pt = FVector(
			cur_bone->getWorldStartPt().x,
			cur_bone->getWorldStartPt().z,
			cur_bone->getWorldStartPt().y);
		auto body_pt = base_xform.TransformPosition(bone_start_pt);
		setupBoxSettings(new_body, body_pt, false);

		bodies.Add(cur_bone->getKey().ToString(), boxAndBone(new_body, cur_bone, next_bone, cur_basis));

		// Last bone, add to tip
		if (i == (bones_in.Num() - 1))
		{
			new_body = NewObject<UBoxComponent>(parent);

			// Flip y and z
			auto bone_end_pt = FVector(
				cur_bone->getWorldEndPt().x,
				cur_bone->getWorldEndPt().z,
				cur_bone->getWorldEndPt().y);
			body_pt = base_xform.TransformPosition(bone_end_pt);
			setupBoxSettings(new_body, body_pt, false);

			bodies[cur_bone->getKey().ToString()].end_box = new_body;
			created_bodies.Add(new_body);
		}
	}
	
	auto makeChainConstraints = [](
		UBoxComponent * body1,
		UBoxComponent * body2,
		float stiffness,
		float damping,
		USceneComponent * attach_root,
		UObject * parent)
	{
		FConstraintInstance constraint_inst;
		SetAngularLimits(
			constraint_inst,
			1,
			1,
			1,
			0.0f,
			0.0f,
			0.0f,
			true,
			true,
			stiffness,
			damping,
			stiffness,
			damping);

		SetLinearLimits(
			constraint_inst,
			true,
			2,
			2,
			2,
			2.0f,
			false,
			stiffness * 2.0f,
			damping * 2.0f);

		UPhysicsConstraintComponent* constraint_comp = NewObject<UPhysicsConstraintComponent>(parent);

		auto body_pt = body1->GetComponentLocation();
		//constraint_comp->SetWorldLocation(body_pt);
		//constraint_comp->AttachToComponent(attach_root, FAttachmentTransformRules::KeepWorldTransform);
		constraint_comp->AttachToComponent(body1, FAttachmentTransformRules::KeepRelativeTransform);
		constraint_comp->SetRelativeLocation(FVector::ZeroVector);
		constraint_comp->SetDisableCollision(true);
		constraint_comp->ConstraintInstance = constraint_inst;
		constraint_comp->SetConstrainedComponents(body1, FName(*body1->GetName()), body2, FName(*body2->GetName()));
		//constraint_comp->RegisterComponent();

		return constraint_comp;
	};

	// Create constraints
	for (int i = 0; i < bones_in.Num() - 1; i++)
	{
		auto bone_name_1 = bones_in[i]->getKey().ToString();
		auto bone_name_2 = bones_in[i + 1]->getKey().ToString();
		auto body1 = bodies[bone_name_1].box;
		auto body2 = bodies[bone_name_2].box;

		auto main_constraint = 
			makeChainConstraints(body1, body2, stiffness, damping, attach_root, parent);
		if (getConstraint(bone_name_1, bone_name_2) == nullptr)
		{
			constraints.Add(
				getConstraintsKey(bone_name_1, bone_name_2),
				TArray<UPhysicsConstraintComponent *>());
		}

		auto cur_list = getConstraint(bone_name_1, bone_name_2);
		cur_list->Add(main_constraint);
	}

	{
		auto last_bone_name = bones_in[bones_in.Num() - 1]->getKey().ToString();
		auto last_base_body = bodies[last_bone_name].box;
		auto last_end_body = bodies[last_bone_name].end_box;

		auto last_constraint =
			makeChainConstraints(last_base_body, last_end_body, stiffness, damping, attach_root, parent);
		constraints.Add(
			getConstraintsKey(last_bone_name, last_bone_name),
			TArray<UPhysicsConstraintComponent *>());
		auto cur_list = getConstraint(last_bone_name, last_bone_name);
		cur_list->Add(last_constraint);
	}

	for (int i = 0; i < created_bodies.Num(); i++)
	{
		created_bodies[i]->SetSimulatePhysics((i == 0) ? false : true);
	}
}

void CreaturePhysicsData::clearPhysicsChain()
{
	for (auto cur_data : constraints)
	{
		auto cur_array = cur_data.Value;
		for (auto cur_constraint : cur_array)
		{
			cur_constraint->DestroyComponent();
		}
	}
	
	for (auto cur_data : bodies)
	{
		auto& box_data = cur_data.Value;
		if (box_data.box)
		{
			box_data.box->DestroyComponent();
		}

		if (box_data.end_box)
		{
			box_data.end_box->DestroyComponent();
		}
	}

	constraints.Empty();
	bodies.Empty();
}

void CreaturePhysicsData::updateKinematicPos(
	const FTransform& base_xform, 
	meshBone * bone_in)
{
	if(!bodies.Contains(bone_in->getKey().ToString()))
	{
		return;
	}

	auto set_body = bodies[bone_in->getKey().ToString()].box;
	// Flip y and z
	auto new_pos = FVector(
		bone_in->getWorldStartPt().x,
		bone_in->getWorldStartPt().z,
		bone_in->getWorldStartPt().y
	);
	//set_body->SetWorldLocation(base_xform.TransformPosition(new_pos));
	set_body->SetAllPhysicsPosition(base_xform.TransformPosition(new_pos));
}

void CreaturePhysicsData::updateAllKinematicBones(const FTransform & base_xform)
{
	for (auto cur_bone : kinematic_bones)
	{
		updateKinematicPos(base_xform, cur_bone);
	}
}

void CreaturePhysicsData::updateBonePositions(const FTransform& base_xform)
{
	auto inv_base_xform = base_xform.Inverse();
	for (auto& body_data : bodies)
	{
		auto cur_box = body_data.Value.box;
		auto set_bone = body_data.Value.bone;
		//if (cur_box->IsSimulatingPhysics()) {
		{
			auto char_pos = inv_base_xform.TransformPosition(cur_box->GetComponentLocation());
			// Flip y and z
			auto set_bone_start_pt = glm::vec4(char_pos.X, char_pos.Z, char_pos.Y, 1.0f);
			set_bone->setWorldStartPt(set_bone_start_pt);
		}

		// Compute end pt from basis
		FVector2D bone_dir(0, 0);
		UBoxComponent * next_base_body = nullptr, *next_end_body = nullptr;

		if (body_data.Value.next_bone)
		{
			auto next_data = bodies[body_data.Value.next_bone->getKey().ToString()];
			next_base_body = next_data.box;

			if (next_data.next_bone)
			{
				next_end_body = bodies[next_data.next_bone->getKey().ToString()].box;
			}
			else if (next_data.end_box)
			{
				next_end_body = next_data.end_box;
			}
		}
		else if (body_data.Value.end_box) {
			next_base_body = cur_box;
			next_end_body = body_data.Value.end_box;
		}

		if (next_base_body && next_end_body) {
			auto next_base_pos =
				inv_base_xform.TransformPosition(next_base_body->GetComponentLocation());
			auto next_end_pos =
				inv_base_xform.TransformPosition(next_end_body->GetComponentLocation());
			/*
			auto next_dir = next_end_pos - next_base_pos;
			auto next_unit_dir = next_dir.GetSafeNormal();
			// Swap Y and Z
			FVector2D tangent(next_unit_dir.X, next_unit_dir.Z);
			FVector2D normal(-tangent.Y, tangent.X);

			bone_dir =
				(body_data.Value.basis.X * tangent)
				+ (body_data.Value.basis.Y * normal);
			*/

			if (!body_data.Value.end_box)
			{
				set_bone->setWorldEndPt(
					glm::vec4(next_base_pos.X, next_base_pos.Z, 0.0f, 1.0f));
			}
			else {
				set_bone->setWorldEndPt(glm::vec4(next_end_pos.X, next_end_pos.Z, 0.0f, 1.0f));
			}
		}

	}
}

void CreaturePhysicsData::drawDebugBones(UWorld * world_in)
{
	for (auto cur_body : bodies)
	{
		if (cur_body.Value.box)
		{
			DrawDebugSphere(
				world_in,
				cur_body.Value.box->GetComponentLocation(),
				3.0f,
				32,
				FColor(255, 0, 0)
			);
		}

		if (cur_body.Value.end_box)
		{
			DrawDebugSphere(
				world_in,
				cur_body.Value.end_box->GetComponentLocation(),
				3.0f,
				32,
				FColor(255, 0, 0)
			);
		}
	}
}

// UCreatureMetaAsset
FString& UCreatureMetaAsset::GetJsonString()
{
	return jsonString;
}

void UCreatureMetaAsset::PostLoad()
{
	Super::PostLoad();
	BuildMetaData();
}

#if WITH_EDITORONLY_DATA
void UCreatureMetaAsset::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}

	Super::PostInitProperties();
}
#endif /*WITH_EDITORONLY_DATA*/

void UCreatureMetaAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
}

#if WITH_EDITORONLY_DATA
FString UCreatureMetaAsset::GetSourceFilename() const
{
	TArray<FString> filenames;
	if (AssetImportData)
	{
		AssetImportData->ExtractFilenames(filenames);
	}
	return (filenames.Num() > 0) ? filenames[0] : FString();
}

void UCreatureMetaAsset::SetSourceFilename(const FString &filename)
{
	if (!AssetImportData)
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
	AssetImportData->UpdateFilenameOnly(filename);
}
#endif /*WITH_EDITORONLY_DATA*/

void 
UCreatureMetaAsset::BuildMetaData()
{
	TSharedPtr<FJsonObject> jsonObject = MakeShareable(new FJsonObject);
	TSharedRef< TJsonReader<> > reader = TJsonReaderFactory<>::Create(jsonString);
	meta_data.clear();

	TArray<FBendPhysicsChain> old_bend_physics_chains = bend_physics_chains;
	bend_physics_chains.Empty();

	if (FJsonSerializer::Deserialize(reader, jsonObject))
	{
		// Fill mesh data
		if (jsonObject->HasField(TEXT("meshes"))) {
			auto meshes_obj = jsonObject->GetObjectField(TEXT("meshes"));
			for (auto cur_data : meshes_obj->Values)
			{
				auto cur_json = cur_data.Value->AsObject();
				auto cur_id = cur_json->GetIntegerField(TEXT("id"));
				auto cur_start_index = cur_json->GetIntegerField(TEXT("startIndex"));
				auto cur_end_index = cur_json->GetIntegerField(TEXT("endIndex"));

				meta_data.mesh_map.Add(cur_id, TTuple<int32, int32>(cur_start_index, cur_end_index));
			}
		}

		// Fill switch order data
		if (jsonObject->HasField(TEXT("regionOrders"))) {
			auto orders_obj = jsonObject->GetObjectField(TEXT("regionOrders"));
			for (auto cur_data : orders_obj->Values)
			{
				auto cur_anim_name = cur_data.Key;
				TMap<int32, TArray<int32> > cur_switch_order_map;

				auto cur_obj_array = cur_data.Value->AsArray();
				for (auto cur_switch_json : cur_obj_array)
				{
					auto switch_obj = cur_switch_json->AsObject();
					auto cur_switch_list = switch_obj->GetArrayField(TEXT("switch_order"));

					TArray<int32> cur_switch_ints;
					for (auto cur_switch_val : cur_switch_list)
					{
						cur_switch_ints.Add((int32)cur_switch_val->AsNumber());
					}

					auto cur_switch_time = switch_obj->GetIntegerField(TEXT("switch_time"));
					
					cur_switch_order_map.Add(cur_switch_time, cur_switch_ints);
				}


				meta_data.anim_order_map.Add(cur_anim_name, cur_switch_order_map);
			}
		}

		// Fill event triggers
		event_names.Empty();
		if (jsonObject->HasField(TEXT("eventTriggers"))) {
			auto events_obj = jsonObject->GetObjectField(TEXT("eventTriggers"));
			for (auto cur_data : events_obj->Values)
			{
				auto cur_anim_name = cur_data.Key;
				TMap<int32, FString> cur_events_map;
				auto cur_obj_array = cur_data.Value->AsArray();
				for (auto cur_events_json : cur_obj_array)
				{
					auto cur_events_obj = cur_events_json->AsObject();
					auto cur_event_name = cur_events_obj->GetStringField(TEXT("event_name"));
					auto switch_time = cur_events_obj->GetIntegerField(TEXT("switch_time"));

					cur_events_map.Add(switch_time, cur_event_name);
				}

				meta_data.anim_events_map.Add(cur_anim_name, cur_events_map);
				for (const auto &eventIt : cur_events_map)
				{
					event_names.Add(FString::Printf(TEXT("%s.%s"), *cur_anim_name, *eventIt.Value));
				}
			}
		}

		// Fill Skin Swaps
		skin_swap_names.Empty();
		if (jsonObject->HasField(TEXT("skinSwapList")))
		{
			auto skin_swap_obj = jsonObject->GetObjectField(TEXT("skinSwapList"));
			for (auto cur_data : skin_swap_obj->Values)
			{
				TSet<FString> new_swap_set;
				auto swap_name = cur_data.Key;
				auto swap_data = cur_data.Value->AsObject()->GetObjectField(TEXT("swap"));
				auto swap_items = swap_data->GetArrayField("swap_items");
				for (auto cur_item : swap_items)
				{
					auto item_name = cur_item->AsString();
					new_swap_set.Add(item_name);
				}

				meta_data.skin_swaps.Add(swap_name, new_swap_set);
				skin_swap_names.Add(swap_name);
			}
		}

		// Fill Bend Physics Chains
		if (jsonObject->HasField(TEXT("physicsData"))) {
			auto physics_obj = jsonObject->GetObjectField(TEXT("physicsData"));
			for (auto cur_data : physics_obj->Values)
			{
				auto cur_anim_name = cur_data.Key;
				auto motor_objs = cur_data.Value->AsObject();
				for (auto cur_motor : motor_objs->Values)
				{
					auto motor_name = cur_motor.Key;
					auto bone_objs = cur_motor.Value->AsArray();
					FBendPhysicsChain new_chain;
					new_chain.motor_name = motor_name;
					new_chain.anim_clip_name = cur_anim_name;
					new_chain.num_bones = bone_objs.Num();

					for (auto cur_bone : bone_objs)
					{
						auto bone_name = cur_bone->AsString();
						new_chain.bone_names.Add(bone_name);
					}

					for (auto& old_chain : old_bend_physics_chains)
					{
						if ((old_chain.anim_clip_name == cur_anim_name)
							&& (old_chain.motor_name == motor_name))
						{
							new_chain.active = old_chain.active;
							new_chain.stiffness = old_chain.stiffness;
							new_chain.damping = old_chain.damping;
						}
					}

					bend_physics_chains.Add(new_chain);
				}
			}
		}
	}
}

TSharedPtr<CreaturePhysicsData>
UCreatureMetaAsset::CreateBendPhysicsChain(
	const FTransform& base_xform,
	USceneComponent * attach_root,
	UObject * parent,
	meshRenderBoneComposition * bone_composition, 
	const FString& anim_clip)
{
	auto physics_data = TSharedPtr<CreaturePhysicsData>(new CreaturePhysicsData);
	physics_data->clearPhysicsChain();

	for (auto& chain_data : bend_physics_chains)
	{
		if ((chain_data.anim_clip_name == anim_clip) &&
			(chain_data.active))
		{
			TArray<meshBone *> chain_bones;
			for (auto& bone_name : chain_data.bone_names)
			{
				if (!bone_composition->getBonesMap().Contains(*bone_name))
				{
					return nullptr;
				}

				auto cur_bone = bone_composition->getBonesMap()[*bone_name];
				chain_bones.Add(cur_bone);
			}

			physics_data->createPhysicsChain(
				base_xform,
				attach_root,
				parent,
				chain_bones,
				chain_data.stiffness,
				chain_data.damping,
				anim_clip);

			physics_data->kinematic_bones.Add(chain_bones[0]);
		}
	}

	return physics_data;
}

CreatureMetaData *
UCreatureMetaAsset::GetMetaData()
{
	return &meta_data;
}