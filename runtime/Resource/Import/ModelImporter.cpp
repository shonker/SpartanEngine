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

//= INCLUDES =================================
#include "pch.h"
#include "ModelImporter.h"
#include "../../Core/ProgressTracker.h"
#include "../../Core/ThreadPool.h"
#include "../../RHI/RHI_Vertex.h"
#include "../../RHI/RHI_Texture.h"
#include "../../Rendering/Animation.h"
#include "../../Rendering/Material.h"
#include "../../Rendering/Mesh.h"
#include "../../World/World.h"
#include "../../World/Entity.h"
#include "../../World/Components/Renderable.h"
#include "../../World/Components/Transform.h"
SP_WARNINGS_OFF
#include "assimp/color4.h"
#include "assimp/matrix4x4.h"
#include "assimp/vector2.h"
#include "assimp/quaternion.h"
#include "assimp/scene.h"
#include "assimp/ProgressHandler.hpp"
#include "assimp/version.h"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
SP_WARNINGS_ON
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
using namespace Assimp;
//============================

namespace Spartan
{
    static const uint32_t importer_flags =
        // Switch to engine conventions
        aiProcess_MakeLeftHanded           | // DirectX style.
        aiProcess_FlipUVs                  | // DirectX style.
        aiProcess_FlipWindingOrder         | // DirectX style.
        // Validate and clean up
        aiProcess_ValidateDataStructure    | // Validates the imported scene data structure. This makes sure that all indices are valid, all animations and bones are linked correctly, all material references are correct
        aiProcess_FindDegenerates          | // Convert degenerate primitives to proper lines or points.
        aiProcess_FindInvalidData          | // This step searches all meshes for invalid data, such as zeroed normal vectors or invalid UV coords and removes / fixes them
        aiProcess_RemoveRedundantMaterials | // Searches for redundant/unreferenced materials and removes them
        aiProcess_Triangulate              | // Triangulates all faces of all meshes
        aiProcess_JoinIdenticalVertices    | // Triangulates all faces of all meshes
        aiProcess_SortByPType              | // Splits meshes with more than one primitive type in homogeneous sub-meshes.
        aiProcess_FindInstances            | // This step searches for duplicate meshes and replaces them with references to the first mesh
        // Generate missing normals or UVs
        aiProcess_CalcTangentSpace         | // Calculates the tangents and bitangents for the imported meshes
        aiProcess_GenSmoothNormals         | // Ignored if the mesh already has normals
        aiProcess_GenUVCoords;               // Converts non-UV mappings (such as spherical or cylindrical mapping) to proper texture coordinate channels

        // Any vertex/index optimization flags are not needed since Mesh is using meshoptimizer

    static Matrix convert_matrix(const aiMatrix4x4& transform)
    {
        return Matrix
        (
            transform.a1, transform.b1, transform.c1, transform.d1,
            transform.a2, transform.b2, transform.c2, transform.d2,
            transform.a3, transform.b3, transform.c3, transform.d3,
            transform.a4, transform.b4, transform.c4, transform.d4
        );
    }

    static Vector4 convert_vector4(const aiColor4D& ai_color)
    {
        return Vector4(ai_color.r, ai_color.g, ai_color.b, ai_color.a);
    }

    static Vector3 convert_vector3(const aiVector3D& ai_vector)
    {
        return Vector3(ai_vector.x, ai_vector.y, ai_vector.z);
    }

    static Vector2 convert_vector2(const aiVector2D& ai_vector)
    {
        return Vector2(ai_vector.x, ai_vector.y);
    }

    static Quaternion convert_quaternion(const aiQuaternion& ai_quaternion)
    {
        return Quaternion(ai_quaternion.x, ai_quaternion.y, ai_quaternion.z, ai_quaternion.w);
    }

    static void set_entity_transform(const aiNode* node, Entity* entity)
    {
        SP_ASSERT_MSG(node != nullptr && entity != nullptr, "Invalid parameter(s)");

        // Convert to engine matrix
        const Matrix matrix_engine = convert_matrix(node->mTransformation);

        // Apply position, rotation and scale
        entity->GetTransform()->SetPositionLocal(matrix_engine.GetTranslation());
        entity->GetTransform()->SetRotationLocal(matrix_engine.GetRotation());
        entity->GetTransform()->SetScaleLocal(matrix_engine.GetScale());
    }

    constexpr void compute_node_count(const aiNode* node, uint32_t* count)
    {
        if (!node)
            return;

        (*count)++;

        // Process children
        for (uint32_t i = 0; i < node->mNumChildren; i++)
        {
            compute_node_count(node->mChildren[i], count);
        }
    }

    // Implement Assimp's progress reporting interface
    class AssimpProgress : public ProgressHandler
    {
    public:
        AssimpProgress(const string& file_path)
        {
            m_file_path = file_path;
            m_file_name = FileSystem::GetFileNameFromFilePath(file_path);
        }
        ~AssimpProgress() = default;

        bool Update(float percentage) override { return true; }

        void UpdateFileRead(int current_step, int number_of_steps) override
        {
            // Reading from drive file progress is ignored because it's not called in a consistent manner.
            // At least two calls are needed (start, end), but this can be called only once.
        }

        void UpdatePostProcess(int current_step, int number_of_steps) override
        {
            if (current_step == 0)
            {
                ProgressTracker::GetProgress(ProgressType::model_importing).JobDone(); // "Loading model from drive..."
                ProgressTracker::GetProgress(ProgressType::model_importing).Start(number_of_steps, "Post-processing model...");
            }
            else
            {
                ProgressTracker::GetProgress(ProgressType::model_importing).JobDone();
            }
        }

    private:
        string m_file_path;
        string m_file_name;
    };

    static string texture_try_multiple_extensions(const string& file_path)
    {
        // Remove extension
        const string file_path_no_ext = FileSystem::GetFilePathWithoutExtension(file_path);

        // Check if the file exists using all engine supported extensions
        for (const auto& supported_format : supported_formats_image)
        {
            string new_file_path = file_path_no_ext + supported_format;
            string new_file_path_upper = file_path_no_ext + FileSystem::ConvertToUppercase(supported_format);

            if (FileSystem::Exists(new_file_path))
            {
                return new_file_path;
            }

            if (FileSystem::Exists(new_file_path_upper))
            {
                return new_file_path_upper;
            }
        }

        return file_path;
    }

    static string texture_validate_path(string original_texture_path, const string& file_path)
    {
        // Models usually return a texture path which is relative to the model's directory.
        // However, to load anything, we'll need an absolute path, so we construct it here.
        const string model_dir = FileSystem::GetDirectoryFromFilePath(file_path);
        string full_texture_path = model_dir + original_texture_path;

        // 1. Check if the texture path is valid
        if (FileSystem::Exists(full_texture_path))
            return full_texture_path;

        // 2. Check the same texture path as previously but 
        // this time with different file extensions (jpg, png and so on).
        full_texture_path = texture_try_multiple_extensions(full_texture_path);
        if (FileSystem::Exists(full_texture_path))
            return full_texture_path;

        // At this point we know the provided path is wrong, we will make a few guesses.
        // The most common mistake is that the artist provided a path which is absolute to his computer.

        // 3. Check if the texture is in the same folder as the model
        full_texture_path = model_dir + FileSystem::GetFileNameFromFilePath(full_texture_path);
        if (FileSystem::Exists(full_texture_path))
            return full_texture_path;

        // 4. Check the same texture path as previously but 
        // this time with different file extensions (jpg, png and so on).
        full_texture_path = texture_try_multiple_extensions(full_texture_path);
        if (FileSystem::Exists(full_texture_path))
            return full_texture_path;

        // Give up, no valid texture path was found
        return "";
    }

    static bool load_material_texture(
        Mesh* mesh,
        const string& file_path,
        const bool is_gltf,
        shared_ptr<Material> material,
        const aiMaterial* material_assimp,
        const MaterialTexture texture_type,
        const aiTextureType texture_type_assimp_pbr,
        const aiTextureType texture_type_assimp_legacy
    )
    {
        // Determine if this is a pbr material or not
        aiTextureType type_assimp = aiTextureType_NONE;
        type_assimp = material_assimp->GetTextureCount(texture_type_assimp_pbr) > 0 ? texture_type_assimp_pbr : type_assimp;
        type_assimp = (type_assimp == aiTextureType_NONE) ? (material_assimp->GetTextureCount(texture_type_assimp_legacy) > 0 ? texture_type_assimp_legacy : type_assimp) : type_assimp;

        // Check if the material has any textures
        if (material_assimp->GetTextureCount(type_assimp) == 0)
            return true;

        // Try to get the texture path
        aiString texture_path;
        if (material_assimp->GetTexture(type_assimp, 0, &texture_path) != AI_SUCCESS)
            return false;

        // See if the texture type is supported by the engine
        const string deduced_path = texture_validate_path(texture_path.data, file_path);
        if (!FileSystem::IsSupportedImageFile(deduced_path))
            return false;

        // Add the texture to the model
        mesh->AddTexture(material, texture_type, texture_validate_path(texture_path.data, file_path), is_gltf);

        // FIX: materials that have a diffuse texture should not be tinted black/gray
        if (type_assimp == aiTextureType_BASE_COLOR || type_assimp == aiTextureType_DIFFUSE)
        {
            material->SetProperty(MaterialProperty::ColorR, 1.0f);
            material->SetProperty(MaterialProperty::ColorG, 1.0f);
            material->SetProperty(MaterialProperty::ColorB, 1.0f);
            material->SetProperty(MaterialProperty::ColorA, 1.0f);
        }

        // FIX: Some models pass a normal map as a height map and vice versa, we correct that.
        if (texture_type == MaterialTexture::Normal || texture_type == MaterialTexture::Height)
        {
            if (shared_ptr<RHI_Texture> texture = material->GetTexture_PtrShared(texture_type))
            {
                MaterialTexture proper_type = texture_type;
                proper_type = (proper_type == MaterialTexture::Normal && texture->IsGrayscale()) ? MaterialTexture::Height : proper_type;
                proper_type = (proper_type == MaterialTexture::Height && !texture->IsGrayscale()) ? MaterialTexture::Normal : proper_type;

                if (proper_type != texture_type)
                {
                    material->SetTexture(texture_type, shared_ptr<RHI_Texture>(nullptr));
                    material->SetTexture(proper_type, texture);
                }
            }
        }

        return true;
    }

    static shared_ptr<Material> load_material(Context* context, Mesh* mesh, const string& file_path, const bool is_gltf, const aiMaterial* material_assimp)
    {
        SP_ASSERT(material_assimp != nullptr);

        shared_ptr<Material> material = make_shared<Material>(context);

        // NAME
        aiString name;
        aiGetMaterialString(material_assimp, AI_MATKEY_NAME, &name);

        // Set a resource file path so it can be used by the resource cache
        material->SetResourceFilePath(FileSystem::RemoveIllegalCharacters(FileSystem::GetDirectoryFromFilePath(file_path) + string(name.C_Str()) + EXTENSION_MATERIAL));

        // COLOR
        aiColor4D color_diffuse(1.0f, 1.0f, 1.0f, 1.0f);
        aiGetMaterialColor(material_assimp, AI_MATKEY_COLOR_DIFFUSE, &color_diffuse);

        // OPACITY
        aiColor4D opacity(1.0f, 1.0f, 1.0f, 1.0f);
        aiGetMaterialColor(material_assimp, AI_MATKEY_OPACITY, &opacity);

        // Set color and opacity
        material->SetProperty(MaterialProperty::ColorR, color_diffuse.r);
        material->SetProperty(MaterialProperty::ColorG, color_diffuse.g);
        material->SetProperty(MaterialProperty::ColorB, color_diffuse.b);
        material->SetProperty(MaterialProperty::ColorA, opacity.r);

        //                                                                          Texture type,                Texture type Assimp (PBR),       Texture type Assimp (Legacy/fallback)
        load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Color,      aiTextureType_BASE_COLOR,        aiTextureType_DIFFUSE);
        load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Roughness,  aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SHININESS); // Use specular as fallback
        load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Metallness, aiTextureType_METALNESS,         aiTextureType_AMBIENT);   // Use ambient as fallback
        load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Normal,     aiTextureType_NORMAL_CAMERA,     aiTextureType_NORMALS);
        load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Occlusion,  aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP);
        load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Emission,   aiTextureType_EMISSION_COLOR,    aiTextureType_EMISSIVE);
        load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Height,     aiTextureType_HEIGHT,            aiTextureType_NONE);
        load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::AlphaMask,  aiTextureType_OPACITY,           aiTextureType_NONE);

        material->SetProperty(MaterialProperty::SingleTextureRoughnessMetalness, static_cast<float>(is_gltf));

        return material;
    }

    ModelImporter::ModelImporter(Context* context)
    {
        m_context = context;
        m_world   = context->GetSystem<World>();

        // Get version
        const int major = aiGetVersionMajor();
        const int minor = aiGetVersionMinor();
        const int rev   = aiGetVersionRevision();
        Settings::RegisterThirdPartyLib("Assimp", to_string(major) + "." + to_string(minor) + "." + to_string(rev), "https://github.com/assimp/assimp");
    }

    bool ModelImporter::Load(Mesh* mesh, const string& file_path)
    {
        SP_ASSERT_MSG(mesh != nullptr, "Invalid parameter");

        if (!FileSystem::IsFile(file_path))
        {
            SP_LOG_ERROR("Provided file path doesn't point to an existing file");
            return false;
        }

        // Model params
        m_file_path = file_path;
        m_name      = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        m_mesh      = mesh;
        m_is_gltf   = FileSystem::GetExtensionFromFilePath(file_path) == ".gltf";

        // Set up the importer
        Importer importer;
        // Remove points and lines.
        importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);
        // Remove cameras and lights
        importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_CAMERAS | aiComponent_LIGHTS);
        // Enable progress tracking
        importer.SetPropertyBool(AI_CONFIG_GLOB_MEASURE_TIME, true);
        importer.SetProgressHandler(new AssimpProgress(file_path));

        ProgressTracker::GetProgress(ProgressType::model_importing).Start(1, "Loading model from drive...");

        // Read the 3D model file from disc
        if (const aiScene* scene = importer.ReadFile(file_path, importer_flags))
        {
            // Update progress tracking
            uint32_t job_count = 0;
            compute_node_count(scene->mRootNode, &job_count);
            ProgressTracker::GetProgress(ProgressType::model_importing).Start(job_count, "Parsing model...");

            m_scene         = scene;
            m_has_animation = scene->mNumAnimations != 0;

            // Recursively parse nodes
            ParseNode(scene->mRootNode);

            // Update model geometry
            {
                while (ProgressTracker::GetProgress(ProgressType::model_importing).GetFraction() != 1.0f)
                {
                    SP_LOG_INFO("Waiting for node processing threads to finish before creating GPU buffers...");
                    this_thread::sleep_for(std::chrono::milliseconds(16));
                }

                //mesh->Optimize();
                mesh->ComputeAabb();
                mesh->ComputeNormalizedScale();
                mesh->CreateGpuBuffers();
            }

            // Activate all the newly added entities (they are now thread-safe)
            m_world->ActivateNewEntities();
        }
        else
        {
            ProgressTracker::GetProgress(ProgressType::model_importing).JobDone();
            SP_LOG_ERROR("%s", importer.GetErrorString());
        }

        importer.FreeScene();

        return m_scene != nullptr;
    }

    void ModelImporter::ParseNode(const aiNode* node, shared_ptr<Entity> parent_entity)
    {
        // Create an entity that will match this node.
        // The entity is created as inactive for thread-safety.
        const bool is_active      = false;
        shared_ptr<Entity> entity = m_world->EntityCreate(is_active);

        // Set root entity to mesh
        bool is_root_node = parent_entity == nullptr;
        if (is_root_node)
        {
            m_mesh->SetRootEntity(entity);
        }

        SP_ASSERT(entity != nullptr);

        // Name the entity
        string node_name = is_root_node ? m_name : node->mName.C_Str();
        entity->SetName(m_name); // Set custom name, which is more descriptive than "RootNode"

        // Update progress tracking
        ProgressTracker::GetProgress(ProgressType::model_importing).SetText("Creating entity for " + entity->GetName());

        // Set the transform of parent_node as the parent of the new_entity's transform
        Transform* parent_trans = parent_entity ? parent_entity->GetTransform() : nullptr;
        entity->GetTransform()->SetParent(parent_trans);

        // Apply node transformation
        set_entity_transform(node, entity.get());

        // Process all the node's meshes
        if (node->mNumMeshes > 0)
        {
            PashMeshes(node, entity.get());
        }

        // Process children
        for (uint32_t i = 0; i < node->mNumChildren; i++)
        {
            // Any subsequent nodes are processed in another thread
            //ThreadPool::AddTask([this, i, node, entity]()
            //{
                ParseNode(node->mChildren[i], entity);
            //});
        }

        // Update progress tracking
        ProgressTracker::GetProgress(ProgressType::model_importing).JobDone();
    }

    void ModelImporter::PashMeshes(const aiNode* assimp_node, Entity* node_entity)
    {
        // An aiNode can have any number of meshes (albeit typically, it's one).
        // If it has more than one meshes, then we create children entities to store them.

        SP_ASSERT_MSG(assimp_node->mNumMeshes != 0, "No meshes to process");

        for (uint32_t i = 0; i < assimp_node->mNumMeshes; i++)
        {
            Entity* entity    = node_entity;
            aiMesh* node_mesh = m_scene->mMeshes[assimp_node->mMeshes[i]];
            string node_name  = assimp_node->mName.C_Str();

            // if this node has more than one meshes, create an entity for each mesh, then make that entity a child of node_entity
            if (assimp_node->mNumMeshes > 1)
            {
                // Create entity
                bool is_active = false;
                entity         = m_world->EntityCreate(is_active).get();

                // Set parent
                entity->GetTransform()->SetParent(node_entity->GetTransform());

                // Set name
                node_name += "_" + to_string(i + 1); // set name
            }

            // Set entity name
            entity->SetName(node_name);
            
            // Load the mesh onto the entity (via a Renderable component)
            ParseMesh(node_mesh, entity);
        }
    }

    void ModelImporter::ParseMesh(aiMesh* assimp_mesh, Entity* entity_parent)
    {
        SP_ASSERT(assimp_mesh != nullptr);
        SP_ASSERT(entity_parent != nullptr);

        const uint32_t vertex_count = assimp_mesh->mNumVertices;
        const uint32_t index_count  = assimp_mesh->mNumFaces * 3;

        // Vertices
        vector<RHI_Vertex_PosTexNorTan> vertices = vector<RHI_Vertex_PosTexNorTan>(vertex_count);
        {
            for (uint32_t i = 0; i < vertex_count; i++)
            {
                RHI_Vertex_PosTexNorTan& vertex = vertices[i];

                // Position
                const aiVector3D& pos = assimp_mesh->mVertices[i];
                vertex.pos[0] = pos.x;
                vertex.pos[1] = pos.y;
                vertex.pos[2] = pos.z;

                // Normal
                if (assimp_mesh->mNormals)
                {
                    const aiVector3D& normal = assimp_mesh->mNormals[i];
                    vertex.nor[0] = normal.x;
                    vertex.nor[1] = normal.y;
                    vertex.nor[2] = normal.z;
                }

                // Tangent
                if (assimp_mesh->mTangents)
                {
                    const aiVector3D& tangent = assimp_mesh->mTangents[i];
                    vertex.tan[0] = tangent.x;
                    vertex.tan[1] = tangent.y;
                    vertex.tan[2] = tangent.z;
                }

                // Texture coordinates
                const uint32_t uv_channel = 0;
                if (assimp_mesh->HasTextureCoords(uv_channel))
                {
                    const auto& tex_coords = assimp_mesh->mTextureCoords[uv_channel][i];
                    vertex.tex[0] = tex_coords.x;
                    vertex.tex[1] = tex_coords.y;
                }
            }
        }

        // Indices
        vector<uint32_t> indices = vector<uint32_t>(index_count);
        {
            // Get indices by iterating through each face of the mesh.
            for (uint32_t face_index = 0; face_index < assimp_mesh->mNumFaces; face_index++)
            {
                // if (aiPrimitiveType_LINE | aiPrimitiveType_POINT) && aiProcess_Triangulate) then (face.mNumIndices == 3)
                const aiFace& face           = assimp_mesh->mFaces[face_index];
                const uint32_t indices_index = (face_index * 3);
                indices[indices_index + 0]   = face.mIndices[0];
                indices[indices_index + 1]   = face.mIndices[1];
                indices[indices_index + 2]   = face.mIndices[2];
            }
        }

        // Compute AABB (before doing move operation on vertices)
        const BoundingBox aabb = BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size()));

        // Add the mesh to the model
        uint32_t index_offset  = 0;
        uint32_t vertex_offset = 0;
        m_mesh->AddIndices(indices, &index_offset);
        m_mesh->AddVertices(vertices, &vertex_offset);

        // Add a renderable component to this entity
        Renderable* renderable = entity_parent->AddComponent<Renderable>();

        // Set the geometry
        renderable->SetGeometry(
            entity_parent->GetName(),
            index_offset,
            static_cast<uint32_t>(indices.size()),
            vertex_offset,
            static_cast<uint32_t>(vertices.size()),
            aabb,
            m_mesh
        );

        // Material
        if (m_scene->HasMaterials())
        {
            // Get aiMaterial
            const aiMaterial* assimp_material = m_scene->mMaterials[assimp_mesh->mMaterialIndex];

            // Convert it and add it to the model
            shared_ptr<Material> material = load_material(m_context, m_mesh, m_file_path, m_is_gltf, assimp_material);

            m_mesh->AddMaterial(material, entity_parent->GetPtrShared());
        }

        // Bones
        LoadBones(assimp_mesh);
    }

    void ModelImporter::ParseAnimations()
    {
        for (uint32_t i = 0; i < m_scene->mNumAnimations; i++)
        {
            const auto assimp_animation = m_scene->mAnimations[i];
            auto animation = make_shared<Animation>(m_context);

            // Basic properties
            animation->SetName(assimp_animation->mName.C_Str());
            animation->SetDuration(assimp_animation->mDuration);
            animation->SetTicksPerSec(assimp_animation->mTicksPerSecond != 0.0f ? assimp_animation->mTicksPerSecond : 25.0f);

            // Animation channels
            for (uint32_t j = 0; j < static_cast<uint32_t>(assimp_animation->mNumChannels); j++)
            {
                const auto assimp_node_anim = assimp_animation->mChannels[j];
                AnimationNode animation_node;

                animation_node.name = assimp_node_anim->mNodeName.C_Str();

                // Position keys
                for (uint32_t k = 0; k < static_cast<uint32_t>(assimp_node_anim->mNumPositionKeys); k++)
                {
                    const auto time = assimp_node_anim->mPositionKeys[k].mTime;
                    const auto value = convert_vector3(assimp_node_anim->mPositionKeys[k].mValue);

                    animation_node.positionFrames.emplace_back(KeyVector{ time, value });
                }

                // Rotation keys
                for (uint32_t k = 0; k < static_cast<uint32_t>(assimp_node_anim->mNumRotationKeys); k++)
                {
                    const auto time = assimp_node_anim->mPositionKeys[k].mTime;
                    const auto value = convert_quaternion(assimp_node_anim->mRotationKeys[k].mValue);

                    animation_node.rotationFrames.emplace_back(KeyQuaternion{ time, value });
                }

                // Scaling keys
                for (uint32_t k = 0; k < static_cast<uint32_t>(assimp_node_anim->mNumScalingKeys); k++)
                {
                    const auto time = assimp_node_anim->mPositionKeys[k].mTime;
                    const auto value = convert_vector3(assimp_node_anim->mScalingKeys[k].mValue);

                    animation_node.scaleFrames.emplace_back(KeyVector{ time, value });
                }
            }
        }
    }

    void ModelImporter::LoadBones(const aiMesh* assimp_mesh)
    {
        // Maximum number of bones per mesh
        // Must not be higher than same const in skinning shader
        constexpr uint8_t MAX_BONES = 64;
        // Maximum number of bones per vertex
        constexpr uint8_t MAX_BONES_PER_VERTEX = 4;

        //for (uint32_t i = 0; i < assimp_mesh->mNumBones; i++)
        //{
        //    uint32_t index = 0;

        //    assert(assimp_mesh->mNumBones <= MAX_BONES);

        //    string name = assimp_mesh->mBones[i]->mName.data;

        //    if (boneMapping.find(name) == boneMapping.end())
        //    {
        //        // Bone not present, add new one
        //        index = numBones;
        //        numBones++;
        //        BoneInfo bone;
        //        boneInfo.push_back(bone);
        //        boneInfo[index].offset = pMesh->mBones[i]->mOffsetMatrix;
        //        boneMapping[name] = index;
        //    }
        //    else
        //    {
        //        index = boneMapping[name];
        //    }

        //    for (uint32_t j = 0; j < assimp_mesh->mBones[i]->mNumWeights; j++)
        //    {
        //        uint32_t vertexID = vertexOffset + pMesh->mBones[i]->mWeights[j].mVertexId;
        //        Bones[vertexID].add(index, pMesh->mBones[i]->mWeights[j].mWeight);
        //    }
        //}
        //boneTransforms.resize(numBones);
    }
}
