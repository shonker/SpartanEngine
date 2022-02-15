/*
Copyright(c) 2016-2022 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ====================================
#include "Properties.h"
#include "../ImGuiExtension.h"
#include "../ImGui/Source/imgui_stdlib.h"
#include "../ImGui/Source/imgui_internal.h"
#include "../WidgetsDeferred/ButtonColorPicker.h"
#include "Core/Engine.h"
#include "Rendering/Model.h"
#include "World/Entity.h"
#include "World/Components/Transform.h"
#include "World/Components/Renderable.h"
#include "World/Components/RigidBody.h"
#include "World/Components/SoftBody.h"
#include "World/Components/Collider.h"
#include "World/Components/Constraint.h"
#include "World/Components/Light.h"
#include "World/Components/AudioSource.h"
#include "World/Components/AudioListener.h"
#include "World/Components/Script.h"
#include "World/Components/Environment.h"
#include "World/Components/Terrain.h"
#include "World/Components/ReflectionProbe.h"
//===============================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
//======================

weak_ptr<Entity> Properties::m_inspected_entity;
weak_ptr<Material> Properties::m_inspected_material;

namespace helper
{
    static ResourceCache* resource_cache;
    static World* world;
    static Vector3 rotation_hint;

    static string g_contex_menu_id;
    static float g_column = 180.0f;
    static const float g_max_width = 100.0f;
    static IComponent* g_copied;

    inline void ComponentContextMenu_Options(const string& id, IComponent* component, const bool removable)
    {
        if (ImGui::BeginPopup(id.c_str()))
        {
            if (removable)
            {
                if (ImGui::MenuItem("Remove"))
                {
                    if (auto entity = Properties::m_inspected_entity.lock())
                    {
                        if (component)
                        {
                            entity->RemoveComponentById(component->GetObjectId());
                        }
                    }
                }
            }

            if (ImGui::MenuItem("Copy Attributes"))
            {
                g_copied = component;
            }

            if (ImGui::MenuItem("Paste Attributes"))
            {
                if (g_copied && g_copied->GetType() == component->GetType())
                {
                    component->SetAttributes(g_copied->GetAttributes());
                }
            }

            ImGui::EndPopup();
        }
    }

    inline bool ComponentBegin(const string& name, const IconType icon_enum, IComponent* component_instance, bool options = true, const bool removable = true)
    {
        // Collapsible contents
        const bool collapsed = ImGuiEx::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_DefaultOpen);

        // Component Icon - Top left
        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();

        // Component Options - Top right
        if (options)
        {
            const float icon_width = 16.0f;
            const auto original_pen_y = ImGui::GetCursorPosY();

            ImGui::SetCursorPosY(original_pen_y + 5.0f);
            ImGuiEx::Image(icon_enum, 15);
            ImGui::SameLine(ImGuiEx::GetWindowContentRegionWidth() - icon_width + 1.0f); ImGui::SetCursorPosY(original_pen_y);
            if (ImGuiEx::ImageButton(name.c_str(), IconType::Component_Options, icon_width))
            {
                g_contex_menu_id = name;
                ImGui::OpenPopup(g_contex_menu_id.c_str());
            }

            if (g_contex_menu_id == name)
            {
                ComponentContextMenu_Options(g_contex_menu_id, component_instance, removable);
            }
        }

        return collapsed;
    }

    inline void ComponentEnd()
    {
        ImGui::Separator();
    }
}

Properties::Properties(Editor* editor) : Widget(editor)
{
    m_title  = "Properties";
    m_size_initial.x = 500; // min width

    m_colorPicker_light     = make_unique<ButtonColorPicker>("Light Color Picker");
    m_material_color_picker = make_unique<ButtonColorPicker>("Material Color Picker");
    m_colorPicker_camera    = make_unique<ButtonColorPicker>("Camera Color Picker");

    helper::resource_cache  = m_context->GetSubsystem<ResourceCache>();
    helper::world           = m_context->GetSubsystem<World>();
}

void Properties::TickVisible()
{
    // If the world is loading new entities, don't parse their materials
    if (m_context->GetSubsystem<World>()->IsLoading())
        return;

    ImGui::PushItemWidth(helper::g_max_width);

    if (!m_inspected_entity.expired())
    {
        shared_ptr<Entity> entity_ptr = m_inspected_entity.lock();
        Renderable* renderable        = entity_ptr->GetComponent<Renderable>();
        Material* material            = renderable ? renderable->GetMaterial() : nullptr;
        
        ShowTransform(entity_ptr->GetComponent<Transform>());
        ShowLight(entity_ptr->GetComponent<Light>());
        ShowCamera(entity_ptr->GetComponent<Camera>());
        ShowTerrain(entity_ptr->GetComponent<Terrain>());
        ShowEnvironment(entity_ptr->GetComponent<Environment>());
        ShowAudioSource(entity_ptr->GetComponent<AudioSource>());
        ShowAudioListener(entity_ptr->GetComponent<AudioListener>());
        ShowReflectionProbe(entity_ptr->GetComponent<ReflectionProbe>());
        ShowRenderable(renderable);
        ShowMaterial(material);
        ShowRigidBody(entity_ptr->GetComponent<RigidBody>());
        ShowSoftBody(entity_ptr->GetComponent<SoftBody>());
        ShowCollider(entity_ptr->GetComponent<Collider>());
        ShowConstraint(entity_ptr->GetComponent<Constraint>());
        for (auto& script : entity_ptr->GetComponents<Script>())
        {
            ShowScript(script);
        }
        
        ShowAddComponentButton();
        Drop_AutoAddComponents();
    }
    else if (!m_inspected_material.expired())
    {
        ShowMaterial(m_inspected_material.lock().get());
    }

    ImGui::PopItemWidth();
}

void Properties::Inspect(const weak_ptr<Entity>& entity)
{
    m_inspected_entity = entity;

    if (const auto shared_ptr = entity.lock())
    {
        helper::rotation_hint = shared_ptr->GetTransform()->GetRotationLocal().ToEulerAngles();
    }
    else
    {
        helper::rotation_hint = Vector3::Zero;
    }

    // If we were previously inspecting a material, save the changes
    if (!m_inspected_material.expired())
    {
        m_inspected_material.lock()->SaveToFile(m_inspected_material.lock()->GetResourceFilePathNative());
    }
    m_inspected_material.reset();
}

void Properties::Inspect(const weak_ptr<Material>& material)
{
    m_inspected_entity.reset();
    m_inspected_material = material;
}

void Properties::ShowTransform(Transform* transform) const
{
    enum class Axis
    {
        x,
        y,
        z
    };

    if (helper::ComponentBegin("Transform", IconType::Component_Transform, transform, true, false))
    {
        const bool is_playing = m_context->m_engine->EngineMode_IsSet(Engine_Game);

        //= REFLECT ===========================================================================================
        Vector3 position = transform->GetPositionLocal();
        Vector3 rotation = !is_playing ? helper::rotation_hint : transform->GetRotationLocal().ToEulerAngles();
        Vector3 scale    = transform->GetScaleLocal();
        //=====================================================================================================

        const auto show_float = [](Axis axis, float* value)
        {
            const float label_float_spacing = 15.0f;
            const float step                = 0.01f;
            const string format             = "%.4f";

            // Label
            ImGui::TextUnformatted(axis == Axis::x ? "x" : axis == Axis::y ? "y" : "z");
            ImGui::SameLine(label_float_spacing);
            Vector2 pos_post_label = ImGui::GetCursorScreenPos();

            // Float
            ImGui::PushItemWidth(128.0f);
            ImGui::PushID(static_cast<int>(ImGui::GetCursorPosX() + ImGui::GetCursorPosY()));
            ImGuiEx::DragFloatWrap("##no_label", value, step, numeric_limits<float>::lowest(), numeric_limits<float>::max(), format.c_str());
            ImGui::PopID();
            ImGui::PopItemWidth();

            // Axis color
            static const ImU32 color_x  = IM_COL32(168, 46, 2, 255);
            static const ImU32 color_y  = IM_COL32(112, 162, 22, 255);
            static const ImU32 color_z  = IM_COL32(51, 122, 210, 255);
            static const Vector2 size   = Vector2(4.0f, 19.0f);
            static const Vector2 offset = Vector2(5.0f, 4.0);
            pos_post_label += offset;
            ImRect axis_color_rect      = ImRect(pos_post_label.x, pos_post_label.y, pos_post_label.x + size.x, pos_post_label.y + size.y);
            ImGui::GetWindowDrawList()->AddRectFilled(axis_color_rect.Min, axis_color_rect.Max, axis == Axis::x ? color_x : axis == Axis::y ? color_y : color_z);
        };

        const auto show_vector = [&show_float](const char* label, Vector3& vector)
        {
            const float label_indetation = 15.0f;

            ImGui::BeginGroup();
            ImGui::Indent(label_indetation);
            ImGui::TextUnformatted(label);
            ImGui::Unindent(label_indetation);
            show_float(Axis::x, &vector.x);
            show_float(Axis::y, &vector.y);
            show_float(Axis::z, &vector.z);
            ImGui::EndGroup();
        };
       
        show_vector("Position", position);
        ImGui::SameLine();
        show_vector("Rotation", rotation);
        ImGui::SameLine();
        show_vector("Scale", scale);
        
        //= MAP ===================================================================
        if (!is_playing)
        {
            transform->SetPositionLocal(position);
            transform->SetScaleLocal(scale);

            if (rotation != helper::rotation_hint)
            {
                transform->SetRotationLocal(Quaternion::FromEulerAngles(rotation));
                helper::rotation_hint = rotation;
            }
        }
        //=========================================================================
    }
    helper::ComponentEnd();
}

void Properties::ShowLight(Light* light) const
{
    if (!light)
        return;

    if (helper::ComponentBegin("Light", IconType::Component_Light, light))
    {
        //= REFLECT ======================================================================
        static vector<string> types = { "Directional", "Point", "Spot" };
        float intensity             = light->GetIntensity();
        float angle                 = light->GetAngle() * Math::Helper::RAD_TO_DEG * 2.0f;
        bool shadows                = light->GetShadowsEnabled();
        bool shadows_screen_space   = light->GetShadowsScreenSpaceEnabled();
        bool shadows_transparent    = light->GetShadowsTransparentEnabled();
        bool volumetric             = light->GetVolumetricEnabled();
        float bias                  = light->GetBias();
        float normal_bias           = light->GetNormalBias();
        float range                 = light->GetRange();
        float time_of_day           = light->GetTimeOfDay();
        m_colorPicker_light->SetColor(light->GetColor());

        bool is_directional = light->GetLightType() == LightType::Directional;
        //================================================================================

        // Type
        ImGui::Text("Type");
        ImGui::PushItemWidth(110.0f);
        ImGui::SameLine(helper::g_column);
        uint32_t selection_index = static_cast<uint32_t>(light->GetLightType());
        if (ImGuiEx::ComboBox("##LightType", types, &selection_index))
        {
            light->SetLightType(static_cast<LightType>(selection_index));
        }
        ImGui::PopItemWidth();

        // Time of day
        //if (light->GetLightType() == LightType_Directional)
        //{
        //    ImGui::Text("Time of day");
        //    ImGui::SameLine(ComponentProperty::g_column);
        //    ImGui::PushItemWidth(300); ImGuiEx::DragFloatWrap("##lightTime", &time_of_day, 0.1f, 0.0f, 24.0f); ImGui::PopItemWidth();
        //}

        // Color
        ImGui::Text("Color");
        ImGui::SameLine(helper::g_column); m_colorPicker_light->Update();

        // Intensity
        ImGui::Text(is_directional ? "Intensity (Lux)" : "Intensity (Lumens)");
        ImGui::SameLine(helper::g_column);
        float v_speed   = is_directional ? 20.0f : 5.0f;
        float v_max     = is_directional ? 128000.0f : 100000.0f;
        ImGui::PushItemWidth(300); ImGuiEx::DragFloatWrap("##lightIntensity", &intensity, v_speed, 0.0f, v_max); ImGui::PopItemWidth();

        // Shadows
        ImGui::Text("Shadows");
        ImGui::SameLine(helper::g_column); ImGui::Checkbox("##light_shadows", &shadows);

        // Shadow supplements
        ImGui::BeginDisabled(!shadows);
        {
            // Transparent shadows
            ImGui::Text("Transparent Shadows");
            ImGui::SameLine(helper::g_column); ImGui::Checkbox("##light_shadows_transparent", &shadows_transparent);
            ImGuiEx::Tooltip("Allows transparent objects to cast colored translucent shadows");

            // Volumetric
            ImGui::Text("Volumetric");
            ImGui::SameLine(helper::g_column); ImGui::Checkbox("##light_volumetric", &volumetric);
            ImGuiEx::Tooltip("The shadow map is used to determine which parts of the \"air\" should be lit");

        }
        ImGui::EndDisabled();

        // Screen space shadows
        ImGui::Text("Screen Space Shadows");
        ImGui::SameLine(helper::g_column); ImGui::Checkbox("##light_shadows_screen_space", &shadows_screen_space);
        ImGuiEx::Tooltip("Small scale shadows which add detail were surfaces meet, also known as contact shadows");

        // Bias
        ImGui::Text("Bias");
        ImGui::SameLine(helper::g_column);
        ImGui::PushItemWidth(300); ImGui::InputFloat("##lightBias", &bias, 1.0f, 1.0f, "%.0f"); ImGui::PopItemWidth();

        // Normal Bias
        ImGui::Text("Normal Bias");
        ImGui::SameLine(helper::g_column);
        ImGui::PushItemWidth(300); ImGui::InputFloat("##lightNormalBias", &normal_bias, 1.0f, 1.0f, "%.0f"); ImGui::PopItemWidth();

        // Range
        if (light->GetLightType() != LightType::Directional)
        {
            ImGui::Text("Range");
            ImGui::SameLine(helper::g_column);
            ImGui::PushItemWidth(300); ImGuiEx::DragFloatWrap("##lightRange", &range, 0.01f, 0.0f, 1000.0f); ImGui::PopItemWidth();
        }

        // Angle
        if (light->GetLightType() == LightType::Spot)
        {
            ImGui::Text("Angle");
            ImGui::SameLine(helper::g_column);
            ImGui::PushItemWidth(300); ImGuiEx::DragFloatWrap("##lightAngle", &angle, 0.01f, 1.0f, 179.0f); ImGui::PopItemWidth();
        }

        //= MAP =====================================================================================================================
        if (intensity != light->GetIntensity())                            light->SetIntensity(intensity);
        if (shadows != light->GetShadowsEnabled())                         light->SetShadowsEnabled(shadows);
        if (shadows_screen_space != light->GetShadowsScreenSpaceEnabled()) light->SetShadowsScreenSpaceEnabled(shadows_screen_space);
        if (shadows_transparent != light->GetShadowsTransparentEnabled())  light->SetShadowsTransparentEnabled(shadows_transparent);
        if (volumetric != light->GetVolumetricEnabled())                   light->SetVolumetricEnabled(volumetric);
        if (bias != light->GetBias())                                      light->SetBias(bias);
        if (normal_bias != light->GetNormalBias())                         light->SetNormalBias(normal_bias);
        if (angle != light->GetAngle() * Math::Helper::RAD_TO_DEG * 0.5f)  light->SetAngle(angle * Math::Helper::DEG_TO_RAD * 0.5f);
        if (range != light->GetRange())                                    light->SetRange(range);
        if (time_of_day != light->GetTimeOfDay())                          light->SetTimeOfDay(time_of_day);
        if (m_colorPicker_light->GetColor() != light->GetColor())          light->SetColor(m_colorPicker_light->GetColor());
        //===========================================================================================================================
    }
    helper::ComponentEnd();
}

void Properties::ShowRenderable(Renderable* renderable) const
{
    if (!renderable)
        return;

    if (helper::ComponentBegin("Renderable", IconType::Component_Renderable, renderable))
    {
        //= REFLECT =============================================================
        const string& mesh_name = renderable->GeometryName();
        Material* material      = renderable->GetMaterial();
        string material_name    = material ? material->GetResourceName() : "N/A";
        bool cast_shadows       = renderable->GetCastShadows();
        //=======================================================================

        ImGui::Text("Mesh");
        ImGui::SameLine(helper::g_column); ImGui::Text(mesh_name.c_str());

        // Material
        ImGui::Text("Material");
        ImGui::SameLine(helper::g_column);
        ImGui::PushID("##material_name");
        ImGui::PushItemWidth(200.0f);
        ImGui::InputText("", &material_name, ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
        if (auto payload = ImGuiEx::ReceiveDragPayload(ImGuiEx::DragPayloadType::DragPayload_Material))
        {
            renderable->SetMaterial(std::get<const char*>(payload->data));
        }
        ImGui::PopItemWidth();
        ImGui::PopID();

        // Cast shadows
        ImGui::Text("Cast Shadows");
        ImGui::SameLine(helper::g_column); ImGui::Checkbox("##RenderableCastShadows", &cast_shadows);

        //= MAP ===================================================================================
        if (cast_shadows != renderable->GetCastShadows()) renderable->SetCastShadows(cast_shadows);
        //=========================================================================================
    }
    helper::ComponentEnd();
}

void Properties::ShowRigidBody(RigidBody* rigid_body) const
{
    if (!rigid_body)
        return;

    if (helper::ComponentBegin("RigidBody", IconType::Component_RigidBody, rigid_body))
    {
        //= REFLECT ================================================================
        auto mass               = rigid_body->GetMass();
        auto friction           = rigid_body->GetFriction();
        auto friction_rolling   = rigid_body->GetFrictionRolling();
        auto restitution        = rigid_body->GetRestitution();
        auto use_gravity        = rigid_body->GetUseGravity();
        auto is_kinematic       = rigid_body->GetIsKinematic();
        auto freeze_pos_x       = static_cast<bool>(rigid_body->GetPositionLock().x);
        auto freeze_pos_y       = static_cast<bool>(rigid_body->GetPositionLock().y);
        auto freeze_pos_z       = static_cast<bool>(rigid_body->GetPositionLock().z);
        auto freeze_rot_x       = static_cast<bool>(rigid_body->GetRotationLock().x);
        auto freeze_rot_y       = static_cast<bool>(rigid_body->GetRotationLock().y);
        auto freeze_rot_z       = static_cast<bool>(rigid_body->GetRotationLock().z);
        //==========================================================================

        const auto input_text_flags = ImGuiInputTextFlags_CharsDecimal;
        const auto item_width       = 120.0f;
        const auto step             = 0.1f;
        const auto step_fast        = 0.1f;
        const auto precision        = "%.3f";

        // Mass
        ImGui::Text("Mass");
        ImGui::SameLine(helper::g_column); ImGui::PushItemWidth(item_width); ImGui::InputFloat("##RigidBodyMass", &mass, step, step_fast, precision, input_text_flags); ImGui::PopItemWidth();

        // Friction
        ImGui::Text("Friction");
        ImGui::SameLine(helper::g_column); ImGui::PushItemWidth(item_width); ImGui::InputFloat("##RigidBodyFriction", &friction, step, step_fast, precision, input_text_flags); ImGui::PopItemWidth();

        // Rolling Friction
        ImGui::Text("Rolling Friction");
        ImGui::SameLine(helper::g_column); ImGui::PushItemWidth(item_width); ImGui::InputFloat("##RigidBodyRollingFriction", &friction_rolling, step, step_fast, precision, input_text_flags); ImGui::PopItemWidth();

        // Restitution
        ImGui::Text("Restitution");
        ImGui::SameLine(helper::g_column); ImGui::PushItemWidth(item_width); ImGui::InputFloat("##RigidBodyRestitution", &restitution, step, step_fast, precision, input_text_flags); ImGui::PopItemWidth();

        // Use Gravity
        ImGui::Text("Use Gravity");
        ImGui::SameLine(helper::g_column); ImGui::Checkbox("##RigidBodyUseGravity", &use_gravity);

        // Is Kinematic
        ImGui::Text("Is Kinematic");
        ImGui::SameLine(helper::g_column); ImGui::Checkbox("##RigidBodyKinematic", &is_kinematic);

        // Freeze Position
        ImGui::Text("Freeze Position");
        ImGui::SameLine(helper::g_column); ImGui::Text("X");
        ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosX", &freeze_pos_x);
        ImGui::SameLine(); ImGui::Text("Y");
        ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosY", &freeze_pos_y);
        ImGui::SameLine(); ImGui::Text("Z");
        ImGui::SameLine(); ImGui::Checkbox("##RigidFreezePosZ", &freeze_pos_z);

        // Freeze Rotation
        ImGui::Text("Freeze Rotation");
        ImGui::SameLine(helper::g_column); ImGui::Text("X");
        ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotX", &freeze_rot_x);
        ImGui::SameLine(); ImGui::Text("Y");
        ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotY", &freeze_rot_y);
        ImGui::SameLine(); ImGui::Text("Z");
        ImGui::SameLine(); ImGui::Checkbox("##RigidFreezeRotZ", &freeze_rot_z);

        //= MAP ===========================================================================================================================================================================================================
        if (mass != rigid_body->GetMass())                                      rigid_body->SetMass(mass);
        if (friction != rigid_body->GetFriction())                              rigid_body->SetFriction(friction);
        if (friction_rolling != rigid_body->GetFrictionRolling())               rigid_body->SetFrictionRolling(friction_rolling);
        if (restitution != rigid_body->GetRestitution())                        rigid_body->SetRestitution(restitution);
        if (use_gravity != rigid_body->GetUseGravity())                         rigid_body->SetUseGravity(use_gravity);
        if (is_kinematic != rigid_body->GetIsKinematic())                       rigid_body->SetIsKinematic(is_kinematic);
        if (freeze_pos_x != static_cast<bool>(rigid_body->GetPositionLock().x)) rigid_body->SetPositionLock(Vector3(static_cast<float>(freeze_pos_x), static_cast<float>(freeze_pos_y), static_cast<float>(freeze_pos_z)));
        if (freeze_pos_y != static_cast<bool>(rigid_body->GetPositionLock().y)) rigid_body->SetPositionLock(Vector3(static_cast<float>(freeze_pos_x), static_cast<float>(freeze_pos_y), static_cast<float>(freeze_pos_z)));
        if (freeze_pos_z != static_cast<bool>(rigid_body->GetPositionLock().z)) rigid_body->SetPositionLock(Vector3(static_cast<float>(freeze_pos_x), static_cast<float>(freeze_pos_y), static_cast<float>(freeze_pos_z)));
        if (freeze_rot_x != static_cast<bool>(rigid_body->GetRotationLock().x)) rigid_body->SetRotationLock(Vector3(static_cast<float>(freeze_rot_x), static_cast<float>(freeze_rot_y), static_cast<float>(freeze_rot_z)));
        if (freeze_rot_y != static_cast<bool>(rigid_body->GetRotationLock().y)) rigid_body->SetRotationLock(Vector3(static_cast<float>(freeze_rot_x), static_cast<float>(freeze_rot_y), static_cast<float>(freeze_rot_z)));
        if (freeze_rot_z != static_cast<bool>(rigid_body->GetRotationLock().z)) rigid_body->SetRotationLock(Vector3(static_cast<float>(freeze_rot_x), static_cast<float>(freeze_rot_y), static_cast<float>(freeze_rot_z)));
        //=================================================================================================================================================================================================================
    }
    helper::ComponentEnd();
}

void Properties::ShowSoftBody(SoftBody* soft_body) const
{
    if (!soft_body)
        return;

    if (helper::ComponentBegin("SoftBody", IconType::Component_SoftBody, soft_body))
    {
        //= REFLECT ===============================================================
        //=========================================================================

        //= MAP ===================================================================
        //=========================================================================
    }
    helper::ComponentEnd();
}

void Properties::ShowCollider(Collider* collider) const
{
    if (!collider)
        return;

    if (helper::ComponentBegin("Collider", IconType::Component_Collider, collider))
    {
        //= REFLECT =================================================
        static vector<string> shape_types = {
            "Box",
            "Sphere",
            "Static Plane",
            "Cylinder",
            "Capsule",
            "Cone",
            "Mesh"
        };
        bool optimize                   = collider->GetOptimize();
        Vector3 collider_center         = collider->GetCenter();
        Vector3 collider_bounding_box   = collider->GetBoundingBox();
        //===========================================================

        const auto input_text_flags = ImGuiInputTextFlags_CharsDecimal;
        const auto step             = 0.1f;
        const auto step_fast        = 0.1f;
        const auto precision        = "%.3f";

        // Type
        ImGui::Text("Type");
        ImGui::PushItemWidth(110);
        ImGui::SameLine(helper::g_column);
        uint32_t selection_index = static_cast<uint32_t>(collider->GetShapeType());
        if (ImGuiEx::ComboBox("##colliderType", shape_types, &selection_index))
        {
            collider->SetShapeType(static_cast<ColliderShape>(selection_index));
        }
        ImGui::PopItemWidth();

        // Center
        ImGui::Text("Center");
        ImGui::PushItemWidth(110);
        ImGui::SameLine(helper::g_column); ImGui::PushID("colCenterX"); ImGui::InputFloat("X", &collider_center.x, step, step_fast, precision, input_text_flags); ImGui::PopID();
        ImGui::SameLine();                 ImGui::PushID("colCenterY"); ImGui::InputFloat("Y", &collider_center.y, step, step_fast, precision, input_text_flags); ImGui::PopID();
        ImGui::SameLine();                 ImGui::PushID("colCenterZ"); ImGui::InputFloat("Z", &collider_center.z, step, step_fast, precision, input_text_flags); ImGui::PopID();
        ImGui::PopItemWidth();

        // Size
        ImGui::Text("Size");
        ImGui::PushItemWidth(110);
        ImGui::SameLine(helper::g_column); ImGui::PushID("colSizeX"); ImGui::InputFloat("X", &collider_bounding_box.x, step, step_fast, precision, input_text_flags); ImGui::PopID();
        ImGui::SameLine();                 ImGui::PushID("colSizeY"); ImGui::InputFloat("Y", &collider_bounding_box.y, step, step_fast, precision, input_text_flags); ImGui::PopID();
        ImGui::SameLine();                 ImGui::PushID("colSizeZ"); ImGui::InputFloat("Z", &collider_bounding_box.z, step, step_fast, precision, input_text_flags); ImGui::PopID();
        ImGui::PopItemWidth();

        // Optimize
        if (collider->GetShapeType() == ColliderShape_Mesh)
        {
            ImGui::Text("Optimize");
            ImGui::SameLine(helper::g_column); ImGui::Checkbox("##colliderOptimize", &optimize);
        }

        //= MAP =================================================================================================
        if (collider_center != collider->GetCenter())            collider->SetCenter(collider_center);
        if (collider_bounding_box != collider->GetBoundingBox()) collider->SetBoundingBox(collider_bounding_box);
        if (optimize != collider->GetOptimize())                 collider->SetOptimize(optimize);
        //=======================================================================================================
    }
    helper::ComponentEnd();
}

void Properties::ShowConstraint(Constraint* constraint) const
{
    if (!constraint)
        return;

    if (helper::ComponentBegin("Constraint", IconType::Component_AudioSource, constraint))
    {
        //= REFLECT ========================================================================================
        vector<string> constraint_types = {"Point", "Hinge", "Slider", "ConeTwist" };
        auto other_body                 = constraint->GetBodyOther();
        bool other_body_dirty           = false;
        Vector3 position                = constraint->GetPosition();
        Vector3 rotation                = constraint->GetRotation().ToEulerAngles();
        Vector2 high_limit              = constraint->GetHighLimit();
        Vector2 low_limit               = constraint->GetLowLimit();
        string other_body_name          = other_body.expired() ? "N/A" : other_body.lock()->GetObjectName();
        //==================================================================================================

        const auto inputTextFlags   = ImGuiInputTextFlags_CharsDecimal;
        const float step            = 0.1f;
        const float step_fast       = 0.1f;
        const char* precision       = "%.3f";

        // Type
        ImGui::Text("Type");
        ImGui::SameLine(helper::g_column);
        uint32_t selection_index = static_cast<uint32_t>(constraint->GetConstraintType());
        if (ImGuiEx::ComboBox("##constraintType", constraint_types, &selection_index))
        {
            constraint->SetConstraintType(static_cast<ConstraintType>(selection_index));
        }

        // Other body
        ImGui::Text("Other Body"); ImGui::SameLine(helper::g_column);
        ImGui::PushID("##OtherBodyName");
        ImGui::PushItemWidth(200.0f);
        ImGui::InputText("", &other_body_name, ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
        if (auto payload = ImGuiEx::ReceiveDragPayload(ImGuiEx::DragPayloadType::DragPayload_Entity))
        {
            const uint64_t entity_id = get<uint64_t>(payload->data);
            other_body               = helper::world->EntityGetById(entity_id);
            other_body_dirty         = true;
        }
        ImGui::PopItemWidth();
        ImGui::PopID();

        // Position
        ImGui::Text("Position");
        ImGui::SameLine(helper::g_column); ImGui::Text("X");
        ImGui::SameLine(); ImGui::InputFloat("##ConsPosX", &position.x, step, step_fast, precision, inputTextFlags);
        ImGui::SameLine(); ImGui::Text("Y");
        ImGui::SameLine(); ImGui::InputFloat("##ConsPosY", &position.y, step, step_fast, precision, inputTextFlags);
        ImGui::SameLine(); ImGui::Text("Z");
        ImGui::SameLine(); ImGui::InputFloat("##ConsPosZ", &position.z, step, step_fast, precision, inputTextFlags);

        // Rotation
        ImGui::Text("Rotation");
        ImGui::SameLine(helper::g_column); ImGui::Text("X");
        ImGui::SameLine(); ImGui::InputFloat("##ConsRotX", &rotation.x, step, step_fast, precision, inputTextFlags);
        ImGui::SameLine(); ImGui::Text("Y");
        ImGui::SameLine(); ImGui::InputFloat("##ConsRotY", &rotation.y, step, step_fast, precision, inputTextFlags);
        ImGui::SameLine(); ImGui::Text("Z");
        ImGui::SameLine(); ImGui::InputFloat("##ConsRotZ", &rotation.z, step, step_fast, precision, inputTextFlags);

        // High Limit
        ImGui::Text("High Limit");
        ImGui::SameLine(helper::g_column); ImGui::Text("X");
        ImGui::SameLine(); ImGui::InputFloat("##ConsHighLimX", &high_limit.x, step, step_fast, precision, inputTextFlags);
        if (constraint->GetConstraintType() == ConstraintType_Slider)
        {
            ImGui::SameLine(); ImGui::Text("Y");
            ImGui::SameLine(); ImGui::InputFloat("##ConsHighLimY", &high_limit.y, step, step_fast, precision, inputTextFlags);
        }

        // Low Limit
        ImGui::Text("Low Limit");
        ImGui::SameLine(helper::g_column); ImGui::Text("X");
        ImGui::SameLine(); ImGui::InputFloat("##ConsLowLimX", &low_limit.x, step, step_fast, precision, inputTextFlags);
        if (constraint->GetConstraintType() == ConstraintType_Slider)
        {
            ImGui::SameLine(); ImGui::Text("Y");
            ImGui::SameLine(); ImGui::InputFloat("##ConsLowLimY", &low_limit.y, step, step_fast, precision, inputTextFlags);
        }

        //= MAP =======================================================================================================================
        if (other_body_dirty)                                       { constraint->SetBodyOther(other_body); other_body_dirty = false; }
        if (position != constraint->GetPosition())                  constraint->SetPosition(position);
        if (rotation != constraint->GetRotation().ToEulerAngles())  constraint->SetRotation(Quaternion::FromEulerAngles(rotation));
        if (high_limit != constraint->GetHighLimit())               constraint->SetHighLimit(high_limit);
        if (low_limit != constraint->GetLowLimit())                 constraint->SetLowLimit(low_limit);
        //=============================================================================================================================
    }
    helper::ComponentEnd();
}

void Properties::ShowMaterial(Material* material) const
{
    if (!material)
        return;

    if (helper::ComponentBegin("Material", IconType::Component_Material, nullptr, false))
    {
        const float offset_from_pos_x = 160;

        //= REFLECT ==================================================
        Math::Vector2 tiling = material->GetTiling();
        Math::Vector2 offset = material->GetOffset();
        m_material_color_picker->SetColor(material->GetColorAlbedo());
        //============================================================

        // Name
        ImGui::Text("Name");
        ImGui::SameLine(offset_from_pos_x); ImGui::Text(material->GetResourceName().c_str());

        if (material->IsEditable())
        {
            // Texture slots
            {
                const auto show_property = [this, &offset_from_pos_x, &material](const char* name, const char* tooltip, const Material_Property type, bool show_texture, bool show_modifier)
                {
                    // Name
                    if (name)
                    {
                        ImGui::Text(name);
                        
                        if (tooltip)
                        {
                            ImGuiEx::Tooltip(tooltip);
                        }

                        if (show_texture || show_modifier)
                        {
                            ImGui::SameLine(offset_from_pos_x);
                        }
                    }

                    // Texture
                    if (show_texture)
                    {
                        auto setter = [&material, &type](const shared_ptr<RHI_Texture>& texture) { material->SetTextureSlot(type, texture); };
                        ImGuiEx::ImageSlot(material->GetTexture_PtrShared(type), setter);

                        if (show_modifier)
                        {
                            ImGui::SameLine();
                        }
                    }

                    // Modifier
                    if (show_modifier)
                    {
                        if (type == Material_Color)
                        {
                            m_material_color_picker->Update();
                        }
                        else
                        {
                            ImGui::PushID(static_cast<int>(ImGui::GetCursorPosX() + ImGui::GetCursorPosY()));
                            ImGuiEx::DragFloatWrap("", &material->GetProperty(type), 0.004f, 0.0f, 1.0f);
                            ImGui::PopID();
                        }
                    }
                };

                show_property("Clearcoat",            "Extra white specular layer on top of others",                                       Material_Clearcoat,            false, true);
                show_property("Clearcoat roughness",  "Roughness of clearcoat specular",                                                   Material_Clearcoat_Roughness,  false, true);
                show_property("Anisotropic",          "Amount of anisotropy for specular reflection",                                      Material_Anisotropic,          false, true);
                show_property("Anisotropic rotation", "Rotates the direction of anisotropy, with 1.0 going full circle",                   Material_Anisotropic_Rotation, false, true);
                show_property("Sheen",                "Amount of soft velvet like reflection near edges",                                  Material_Sheen,                false, true);
                show_property("Sheen tint",           "Mix between white and using base color for sheen reflection",                       Material_Sheen_Tint,           false, true);
                show_property("Color",                "Diffuse or metal surface color",                                                    Material_Color,                true, true);
                show_property("Roughness",            "Specifies microfacet roughness of the surface for diffuse and specular reflection", Material_Roughness,            true, true);
                show_property("Metallic",             "Blends between a non-metallic and metallic material model",                         Material_Metallic,             true, true);
                show_property("Normal",               "Controls the normals of the base layers",                                           Material_Normal,               true, true);
                show_property("Height",               "Perceived depth for parallax mapping",                                              Material_Height,               true, true);
                show_property("Occlusion",            "Amount of light loss, can be complementary to SSAO",                                Material_Occlusion,            true, false);
                show_property("Emission",             "Light emission from the surface, works nice with bloom",                            Material_Emission,             true, false);
                show_property("Alpha mask",           "Discards pixels",                                                                   Material_AlphaMask,            true, false);
            }                                                                                                                                                             

            // UV
            {
                const float input_width = 128.0f;

                // Tiling
                ImGui::Text("Tiling");
                ImGui::SameLine(offset_from_pos_x); ImGui::Text("X");
                ImGui::PushItemWidth(input_width);
                ImGui::SameLine(); ImGui::InputFloat("##matTilingX", &tiling.x, 0.01f, 0.1f, "%.2f", ImGuiInputTextFlags_CharsDecimal);
                ImGui::SameLine(); ImGui::Text("Y");
                ImGui::SameLine(); ImGui::InputFloat("##matTilingY", &tiling.y, 0.01f, 0.1f, "%.2f", ImGuiInputTextFlags_CharsDecimal);
                ImGui::PopItemWidth();

                // Offset
                ImGui::Text("Offset");
                ImGui::SameLine(offset_from_pos_x); ImGui::Text("X");
                ImGui::PushItemWidth(input_width);
                ImGui::SameLine(); ImGui::InputFloat("##matOffsetX", &offset.x, 0.01f, 0.1f, "%.2f", ImGuiInputTextFlags_CharsDecimal);
                ImGui::SameLine(); ImGui::Text("Y");
                ImGui::SameLine(); ImGui::InputFloat("##matOffsetY", &offset.y, 0.01f, 0.1f, "%.2f", ImGuiInputTextFlags_CharsDecimal);
                ImGui::PopItemWidth();
            }
        }

        //= MAP =============================================================================================================================
        if (tiling != material->GetTiling())                                  material->SetTiling(tiling);
        if (offset != material->GetOffset())                                  material->SetOffset(offset);
        if (m_material_color_picker->GetColor() != material->GetColorAlbedo()) material->SetColorAlbedo(m_material_color_picker->GetColor());
        //===================================================================================================================================
    }

    helper::ComponentEnd();
}

void Properties::ShowCamera(Camera* camera) const
{
    if (!camera)
        return;

    if (helper::ComponentBegin("Camera", IconType::Component_Camera, camera))
    {
        //= REFLECT ========================================================
        vector<string> projection_types = { "Perspective", "Orthographic" };
        float aperture                  = camera->GetAperture();
        float shutter_speed             = camera->GetShutterSpeed();
        float iso                       = camera->GetIso();
        float fov                       = camera->GetFovHorizontalDeg();
        float near_plane                = camera->GetNearPlane();
        float far_plane                 = camera->GetFarPlane();
        bool fps_control_enabled        = camera->GetFpsControlEnabled();
        m_colorPicker_camera->SetColor(camera->GetClearColor());
        //==================================================================

        const auto input_text_flags = ImGuiInputTextFlags_CharsDecimal;

        // Background
        ImGui::Text("Background");
        ImGui::SameLine(helper::g_column); m_colorPicker_camera->Update();

        // Projection
        ImGui::Text("Projection");
        ImGui::SameLine(helper::g_column);
        ImGui::PushItemWidth(115.0f);
        uint32_t selection_index = static_cast<uint32_t>(camera->GetProjectionType());
        if (ImGuiEx::ComboBox("##cameraProjection", projection_types, &selection_index))
        {
            camera->SetProjection(static_cast<ProjectionType>(selection_index));
        }
        ImGui::PopItemWidth();

        // Aperture
        ImGui::SetCursorPosX(helper::g_column);
        ImGuiEx::DragFloatWrap("Aperture (mm)", &aperture, 0.01f, 0.01f, 150.0f);
        ImGuiEx::Tooltip("Size of the lens diaphragm. Controls depth of field and chromatic aberration.");

        // Shutter speed
        ImGui::SetCursorPosX(helper::g_column);
        ImGuiEx::DragFloatWrap("Shutter Speed (sec)", &shutter_speed, 0.0001f, 0.0f, 1.0f, "%.4f");
        ImGuiEx::Tooltip("Length of time for which the camera shutter is open. Controls the amount of motion blur.");

        // ISO
        ImGui::SetCursorPosX(helper::g_column);
        ImGuiEx::DragFloatWrap("ISO", &iso, 0.1f, 0.0f, 2000.0f);
        ImGuiEx::Tooltip("Sensitivity to light. Controls camera noise.");

        // Field of View
        ImGui::SetCursorPosX(helper::g_column);
        ImGuiEx::DragFloatWrap("Field of View", &fov, 0.1f, 1.0f, 179.0f);

        // Clipping Planes
        ImGui::Text("Clipping Planes");
        ImGui::SameLine(helper::g_column);      ImGui::PushItemWidth(130); ImGui::InputFloat("Near", &near_plane, 0.01f, 0.01f, "%.2f", input_text_flags); ImGui::PopItemWidth();
        ImGui::SetCursorPosX(helper::g_column); ImGui::PushItemWidth(130); ImGui::InputFloat("Far", &far_plane, 0.01f, 0.01f, "%.2f", input_text_flags); ImGui::PopItemWidth();

        // FPS Control
        ImGui::Text("FPS Control");
        ImGui::SameLine(helper::g_column); ImGui::Checkbox("##camera_fps_control", &fps_control_enabled);
        ImGuiEx::Tooltip("Enables FPS control while holding down the right mouse button");

        //= MAP =================================================================================================================
        if (aperture != camera->GetAperture())                           camera->SetAperture(aperture);
        if (shutter_speed != camera->GetShutterSpeed())                  camera->SetShutterSpeed(shutter_speed);
        if (iso != camera->GetIso())                                     camera->SetIso(iso);
        if (fov != camera->GetFovHorizontalDeg())                        camera->SetFovHorizontalDeg(fov);
        if (near_plane != camera->GetNearPlane())                        camera->SetNearPlane(near_plane);
        if (far_plane != camera->GetFarPlane())                          camera->SetFarPlane(far_plane);
        if (fps_control_enabled != camera->GetFpsControlEnabled())       camera->SetFpsControlEnabled(fps_control_enabled);
        if (m_colorPicker_camera->GetColor() != camera->GetClearColor()) camera->SetClearColor(m_colorPicker_camera->GetColor());
        //=======================================================================================================================
    }
    helper::ComponentEnd();
}

void Properties::ShowEnvironment(Environment* environment) const
{
    if (!environment)
        return;

    if (helper::ComponentBegin("Environment", IconType::Component_Environment, environment))
    {
        ImGui::Text("Sphere Map");

        ImGuiEx::ImageSlot(environment->GetTexture(), [&environment](const shared_ptr<RHI_Texture>& texture) { environment->SetTexture(texture); } );
    }
    helper::ComponentEnd();
}

void Properties::ShowTerrain(Terrain* terrain) const
{
    if (!terrain)
        return;

    if (helper::ComponentBegin("Terrain", IconType::Component_Terrain, terrain))
    {
        //= REFLECT =====================================
        float min_y             = terrain->GetMinY();
        float max_y             = terrain->GetMaxY();
        const float progress    = terrain->GetProgress();
        //===============================================

        const float cursor_y = ImGui::GetCursorPosY();

        ImGui::BeginGroup();
        {
            ImGui::Text("Height Map");

            ImGuiEx::ImageSlot(terrain->GetHeightMap(), [&terrain](const shared_ptr<RHI_Texture>& texture) { terrain->SetHeightMap(static_pointer_cast<RHI_Texture2D>(texture)); });

            if (ImGuiEx::Button("Generate", ImVec2(82, 0)))
            {
                terrain->GenerateAsync();
            }
        }
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::SetCursorPosY(cursor_y);
        ImGui::BeginGroup();
        {
            ImGui::InputFloat("Min Y", &min_y);
            ImGui::InputFloat("Max Y", &max_y);

            if (progress > 0.0f && progress < 1.0f)
            {
                ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f));
                ImGui::SameLine();
                ImGui::Text(terrain->GetProgressDescription().c_str());
            }
        }
        ImGui::EndGroup();

        //= MAP =================================================
        if (min_y != terrain->GetMinY()) terrain->SetMinY(min_y);
        if (max_y != terrain->GetMaxY()) terrain->SetMaxY(max_y);
        //=======================================================
    }
    helper::ComponentEnd();
}

void Properties::ShowAudioSource(AudioSource* audio_source) const
{
    if (!audio_source)
        return;

    if (helper::ComponentBegin("Audio Source", IconType::Component_AudioSource, audio_source))
    {
        //= REFLECT ===============================================
        string audio_clip_name  = audio_source->GetAudioClipName();
        bool mute               = audio_source->GetMute();
        bool play_on_start      = audio_source->GetPlayOnStart();
        bool loop               = audio_source->GetLoop();
        int priority            = audio_source->GetPriority();
        float volume            = audio_source->GetVolume();
        float pitch             = audio_source->GetPitch();
        float pan               = audio_source->GetPan();
        //=========================================================

        // Audio clip
        ImGui::Text("Audio Clip");
        ImGui::SameLine(helper::g_column); ImGui::PushItemWidth(250.0f);
        ImGui::InputText("##audioSourceAudioClip", &audio_clip_name, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();
        if (auto payload = ImGuiEx::ReceiveDragPayload(ImGuiEx::DragPayloadType::DragPayload_Audio))
        {
            audio_source->SetAudioClip(std::get<const char*>(payload->data));
        }

        // Mute
        ImGui::Text("Mute");
        ImGui::SameLine(helper::g_column); ImGui::Checkbox("##audioSourceMute", &mute);

        // Play on start
        ImGui::Text("Play on Start");
        ImGui::SameLine(helper::g_column); ImGui::Checkbox("##audioSourcePlayOnStart", &play_on_start);

        // Loop
        ImGui::Text("Loop");
        ImGui::SameLine(helper::g_column); ImGui::Checkbox("##audioSourceLoop", &loop);

        // Priority
        ImGui::Text("Priority");
        ImGui::SameLine(helper::g_column); ImGui::SliderInt("##audioSourcePriority", &priority, 0, 255);

        // Volume
        ImGui::Text("Volume");
        ImGui::SameLine(helper::g_column); ImGui::SliderFloat("##audioSourceVolume", &volume, 0.0f, 1.0f);

        // Pitch
        ImGui::Text("Pitch");
        ImGui::SameLine(helper::g_column); ImGui::SliderFloat("##audioSourcePitch", &pitch, 0.0f, 3.0f);

        // Pan
        ImGui::Text("Pan");
        ImGui::SameLine(helper::g_column); ImGui::SliderFloat("##audioSourcePan", &pan, -1.0f, 1.0f);

        //= MAP ============================================================================================
        if (mute != audio_source->GetMute())                    audio_source->SetMute(mute);
        if (play_on_start != audio_source->GetPlayOnStart())    audio_source->SetPlayOnStart(play_on_start);
        if (loop != audio_source->GetLoop())                    audio_source->SetLoop(loop);
        if (priority != audio_source->GetPriority())            audio_source->SetPriority(priority);
        if (volume != audio_source->GetVolume())                audio_source->SetVolume(volume);
        if (pitch != audio_source->GetPitch())                  audio_source->SetPitch(pitch);
        if (pan != audio_source->GetPan())                      audio_source->SetPan(pan);
        //==================================================================================================
    }
    helper::ComponentEnd();
}

void Properties::ShowAudioListener(AudioListener* audio_listener) const
{
    if (!audio_listener)
        return;

    if (helper::ComponentBegin("Audio Listener", IconType::Component_AudioListener, audio_listener))
    {

    }
    helper::ComponentEnd();
}

void Properties::ShowReflectionProbe(Spartan::ReflectionProbe* reflection_probe) const
{
    if (!reflection_probe)
        return;

    if (helper::ComponentBegin("Reflection Probe", IconType::Component_ReflectionProbe, reflection_probe))
    {
        //= REFLECT =============================================================
        int resolution             = reflection_probe->GetResolution();
        Vector3 extents            = reflection_probe->GetExtents();
        int update_interval_frames = reflection_probe->GetUpdateIntervalFrames();
        int update_face_count      = reflection_probe->GetUpdateFaceCount();
        float plane_near           = reflection_probe->GetNearPlane();
        float plane_far            = reflection_probe->GetFarPlane();
        //=======================================================================

        // Resolution
        ImGui::Text("Resolution");
        ImGui::SameLine(helper::g_column);
        ImGui::PushItemWidth(300); ImGui::InputInt("##reflection_probe_resolution", &resolution); ImGui::PopItemWidth();

        // Update interval frames
        ImGui::Text("Update interval frames");
        ImGui::SameLine(helper::g_column);
        ImGui::PushItemWidth(300); ImGui::InputInt("##reflection_probe_update_interval_frames", &update_interval_frames); ImGui::PopItemWidth();

        // Update face count
        ImGui::Text("Update face count");
        ImGui::SameLine(helper::g_column);
        ImGui::PushItemWidth(300); ImGui::InputInt("##reflection_probe_update_face_count", &update_face_count); ImGui::PopItemWidth();

        // Near plane
        ImGui::Text("Near plane");
        ImGui::SameLine(helper::g_column);
        ImGui::PushItemWidth(300); ImGui::InputFloat("##reflection_probe_plane_near", &plane_near, 1.0f, 1.0f, "%.1f"); ImGui::PopItemWidth();

        // Far plane
        ImGui::Text("Far plane");
        ImGui::SameLine(helper::g_column);
        ImGui::PushItemWidth(300); ImGui::InputFloat("##reflection_probe_plane_far", &plane_far, 1.0f, 1.0f, "%.1f"); ImGui::PopItemWidth();

        // Extents
        const ImGuiInputTextFlags_ input_text_flags = ImGuiInputTextFlags_CharsDecimal;
        const float step                            = 0.1f;
        const float step_fast                       = 0.1f;
        const char* precision                       = "%.3f";
        ImGui::Text("Extents");
        ImGui::PushItemWidth(120);
        ImGui::SameLine(helper::g_column); ImGui::PushID("##reflection_probe_extents_x"); ImGui::InputFloat("X", &extents.x, step, step_fast, precision, input_text_flags); ImGui::PopID();
        ImGui::SameLine();                 ImGui::PushID("##reflection_probe_extents_y"); ImGui::InputFloat("Y", &extents.y, step, step_fast, precision, input_text_flags); ImGui::PopID();
        ImGui::SameLine();                 ImGui::PushID("##reflection_probe_extents_z"); ImGui::InputFloat("Z", &extents.z, step, step_fast, precision, input_text_flags); ImGui::PopID();
        ImGui::PopItemWidth();

        //= MAP =====================================================================================================================================
        if (resolution             != reflection_probe->GetResolution())           reflection_probe->SetResolution(resolution);
        if (extents                != reflection_probe->GetExtents())              reflection_probe->SetExtents(extents);
        if (update_interval_frames != reflection_probe->GetUpdateIntervalFrames()) reflection_probe->SetUpdateIntervalFrames(update_interval_frames);
        if (update_face_count      != reflection_probe->GetUpdateFaceCount())      reflection_probe->SetUpdateFaceCount(update_face_count);
        if (plane_near             != reflection_probe->GetNearPlane())            reflection_probe->SetNearPlane(plane_near);
        if (plane_far              != reflection_probe->GetFarPlane())             reflection_probe->SetFarPlane(plane_far);
        //===========================================================================================================================================
    }
    helper::ComponentEnd();
}

void Properties::ShowScript(Script* script) const
{
    if (!script)
        return;

    if (helper::ComponentBegin(script->GetObjectName(), IconType::Component_Script, script))
    {
        //= REFLECT =========================
        auto script_name = script->GetObjectName();
        //===================================

        ImGui::Text("Script");
        ImGui::SameLine();
        ImGui::PushID("##ScriptNameTemp");
        ImGui::PushItemWidth(200.0f);
        ImGui::InputText("", &script_name, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();
        ImGui::PopID();
    }
    helper::ComponentEnd();
}

void Properties::ShowAddComponentButton() const
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f - 50);
    if (ImGuiEx::Button("Add Component"))
    {
        ImGui::OpenPopup("##ComponentContextMenu_Add");
    }
    ComponentContextMenu_Add();
}

void Properties::ComponentContextMenu_Add() const
{
    if (ImGui::BeginPopup("##ComponentContextMenu_Add"))
    {
        if (auto entity = m_inspected_entity.lock())
        {
            // CAMERA
            if (ImGui::MenuItem("Camera"))
            {
                entity->AddComponent<Camera>();
            }

            // LIGHT
            if (ImGui::BeginMenu("Light"))
            {
                if (ImGui::MenuItem("Directional"))
                {
                    entity->AddComponent<Light>()->SetLightType(LightType::Directional);
                }
                else if (ImGui::MenuItem("Point"))
                {
                    entity->AddComponent<Light>()->SetLightType(LightType::Point);
                }
                else if (ImGui::MenuItem("Spot"))
                {
                    entity->AddComponent<Light>()->SetLightType(LightType::Spot);
                }

                ImGui::EndMenu();
            }

            // PHYSICS
            if (ImGui::BeginMenu("Physics"))
            {
                if (ImGui::MenuItem("Rigid Body"))
                {
                    entity->AddComponent<RigidBody>();
                }
                else if (ImGui::MenuItem("Soft Body"))
                {
                    entity->AddComponent<SoftBody>();
                }
                else if (ImGui::MenuItem("Collider"))
                {
                    entity->AddComponent<Collider>();
                }
                else if (ImGui::MenuItem("Constraint"))
                {
                    entity->AddComponent<Constraint>();
                }

                ImGui::EndMenu();
            }

            // AUDIO
            if (ImGui::BeginMenu("Audio"))
            {
                if (ImGui::MenuItem("Audio Source"))
                {
                    entity->AddComponent<AudioSource>();
                }
                else if (ImGui::MenuItem("Audio Listener"))
                {
                    entity->AddComponent<AudioListener>();
                }

                ImGui::EndMenu();
            }

            // ENVIRONMENT
            if (ImGui::BeginMenu("Environment"))
            {
                if (ImGui::MenuItem("Environment"))
                {
                    entity->AddComponent<Environment>()->LoadDefault();
                }

                ImGui::EndMenu();
            }

            // TERRAIN
            if (ImGui::MenuItem("Terrain"))
            {
                entity->AddComponent<Terrain>();
            }

            // PROBE
            if (ImGui::BeginMenu("Probe"))
            {
                if (ImGui::MenuItem("Reflection Probe"))
                {
                    entity->AddComponent<ReflectionProbe>();
                }

                ImGui::EndMenu();
            }
        }

        ImGui::EndPopup();
    }
}

void Properties::Drop_AutoAddComponents() const
{
    if (auto payload = ImGuiEx::ReceiveDragPayload(ImGuiEx::DragPayloadType::DragPayload_Script))
    {
        if (auto script_component = m_inspected_entity.lock()->AddComponent<Script>())
        {
            script_component->SetScript(get<const char*>(payload->data));
        }
    }
}
