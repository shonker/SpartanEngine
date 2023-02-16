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

//= INCLUDES ================================
#include "pch.h"
#include "Mesh.h"
#include "Context.h"
#include "Renderer.h"
#include "../RHI/RHI_Vertex.h"
#include "../RHI/RHI_Texture.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_IndexBuffer.h"
#include "../RHI/RHI_Texture2D.h"
#include "../World/Components/Renderable.h"
#include "../World/Entity.h"
#include "../Resource/ResourceCache.h"
#include "../IO/FileStream.h"
#include "../Resource/Import/ModelImporter.h"
#include "../World/Components/Transform.h"
SP_WARNINGS_OFF
#include "meshoptimizer/meshoptimizer.h"
SP_WARNINGS_ON
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{

    Mesh::Mesh(Context* context) : IResource(context, ResourceType::Mesh)
    {
        m_flags = GetDefaultFlags();
    }

    void Mesh::Clear()
    {
        m_indices.clear();
        m_indices.shrink_to_fit();

        m_vertices.clear();
        m_vertices.shrink_to_fit();
    }

    bool Mesh::LoadFromFile(const string& file_path)
    {
        const Stopwatch timer;

        if (file_path.empty() || FileSystem::IsDirectory(file_path))
        {
            SP_LOG_WARNING("Invalid file path");
            return false;
        }

        // Load engine format
        if (FileSystem::GetExtensionFromFilePath(file_path) == EXTENSION_MODEL)
        {
            // Deserialize
            auto file = make_unique<FileStream>(file_path, FileStream_Read);
            if (!file->IsOpen())
                return false;

            SetResourceFilePath(file->ReadAs<string>());
            file->Read(&m_normalized_scale);
            file->Read(&m_indices);
            file->Read(&m_vertices);

            //Optimize();
            ComputeAabb();
            ComputeNormalizedScale();
            CreateGpuBuffers();
        }
        // Load foreign format
        else
        {
            SetResourceFilePath(file_path);

            if (ResourceCache::GetModelImporter()->Load(this, file_path))
            {
                // Set the normalized scale to the root entity's transform
                m_normalized_scale = ComputeNormalizedScale();
                m_root_entity.lock()->GetComponent<Transform>()->SetScale(m_normalized_scale);
            }
            else
            {
                return false;
            }
        }

        // Compute memory usage
        {
            // Cpu
            m_object_size_cpu = GetMemoryUsage();

            // Gpu
            if (m_vertex_buffer && m_index_buffer)
            {
                m_object_size_gpu = m_vertex_buffer->GetObjectSizeGpu();
                m_object_size_gpu += m_index_buffer->GetObjectSizeGpu();
            }
        }

        SP_LOG_INFO("Loading \"%s\" took %d ms", FileSystem::GetFileNameFromFilePath(file_path).c_str(), static_cast<int>(timer.GetElapsedTimeMs()));

        return true;
    }

    bool Mesh::SaveToFile(const string& file_path)
    {
        auto file = make_unique<FileStream>(file_path, FileStream_Write);
        if (!file->IsOpen())
            return false;

        file->Write(GetResourceFilePath());
        file->Write(m_normalized_scale);
        file->Write(m_indices);
        file->Write(m_vertices);

        file->Close();

        return true;
    }

    uint32_t Mesh::GetMemoryUsage() const
    {
        uint32_t size = 0;
        size += uint32_t(m_indices.size()  * sizeof(uint32_t));
        size += uint32_t(m_vertices.size() * sizeof(RHI_Vertex_PosTexNorTan));

        return size;
    }

    void Mesh::GetGeometry(uint32_t index_offset, uint32_t index_count, uint32_t vertex_offset, uint32_t vertex_count, vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices)
    {
        SP_ASSERT_MSG(indices != nullptr || vertices != nullptr, "Indices and vertices vectors can't both be null");

        if (indices)
        {
            SP_ASSERT_MSG(index_count != 0, "Index count can't be 0");

            const auto index_first = m_indices.begin() + index_offset;
            const auto index_last  = m_indices.begin() + index_offset + index_count;
            *indices               = vector<uint32_t>(index_first, index_last);
        }

        if (vertices)
        {
            SP_ASSERT_MSG(vertex_count != 0, "Index count can't be 0");

            const auto vertex_first = m_vertices.begin() + vertex_offset;
            const auto vertex_last  = m_vertices.begin() + vertex_offset + vertex_count;
            *vertices               = vector<RHI_Vertex_PosTexNorTan>(vertex_first, vertex_last);
        }
    }

    void Mesh::AddVertices(const vector<RHI_Vertex_PosTexNorTan>& vertices, uint32_t* vertex_offset_out /*= nullptr*/)
    {
        lock_guard lock(m_mutex_add_verices);

        if (vertex_offset_out)
        {
            *vertex_offset_out = static_cast<uint32_t>(m_vertices.size());
        }

        m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
    }

    void Mesh::AddIndices(const vector<uint32_t>& indices, uint32_t* index_offset_out /*= nullptr*/)
    {
        lock_guard lock(m_mutex_add_verices);

        if (index_offset_out)
        {
            *index_offset_out = static_cast<uint32_t>(m_indices.size());
        }

        m_indices.insert(m_indices.end(), indices.begin(), indices.end());
    }

    uint32_t Mesh::GetVertexCount() const
    {
        return static_cast<uint32_t>(m_vertices.size());
    }

    uint32_t Mesh::GetIndexCount() const
    {
        return static_cast<uint32_t>(m_indices.size());
    }

    void Mesh::ComputeAabb()
    {
        SP_ASSERT_MSG(m_vertices.size() != 0, "There are no vertices");

        m_aabb = BoundingBox(m_vertices.data(), static_cast<uint32_t>(m_vertices.size()));
    }

    uint32_t Mesh::GetDefaultFlags()
    {
        return
            (1U << static_cast<uint32_t>(MeshOptions::RemoveRedundantData));
    }
    
    float Mesh::ComputeNormalizedScale()
    {
        // Compute scale offset
        const auto scale_offset = m_aabb.GetExtents().Length();
    
        // Return normalized scale
        return 1.0f / scale_offset;
    }
    
    void Mesh::Optimize()
    {
        // Running this causes object picking to crash for some meshes.
        // I need to fix that then enable this.
        return;

        SP_ASSERT_MSG(!m_indices.empty() && !m_vertices.empty(), "Invalid data");

        uint32_t index_count                     = static_cast<uint32_t>(m_indices.size());
        uint32_t vertex_count                    = static_cast<uint32_t>(m_vertices.size());
        size_t vertex_size                       = sizeof(RHI_Vertex_PosTexNorTan);
        vector<uint32_t> indices                 = m_indices;
        vector<RHI_Vertex_PosTexNorTan> vertices = m_vertices;

        // The optimization order is important

        // Vertex cache optimization - reordering triangles to maximize cache locality
        SP_LOG_INFO("Optimizing vertex cache...");
        meshopt_optimizeVertexCache(&indices[0], &m_indices[0], index_count, vertex_count);

        // Overdraw optimizations - reorders triangles to minimize overdraw from all directions
        SP_LOG_INFO("Optimizing overdraw...");
        meshopt_optimizeOverdraw(&m_indices[0], &indices[0], index_count, &m_vertices[0].pos[0], vertex_count, vertex_size, 1.05f);

        // Vertex fetch optimization - reorders triangles to maximize memory access locality
        SP_LOG_INFO("Optimizing vertex fetch...");
        meshopt_optimizeVertexFetch(&m_vertices[0], &m_indices[0], index_count, &vertices[0], vertex_count, vertex_size);
    }

    void Mesh::CreateGpuBuffers()
    {
        RHI_Device* rhi_device = m_context->GetSystem<Renderer>()->GetRhiDevice().get();

        SP_ASSERT_MSG(!m_indices.empty(), "There are no indices");
        m_index_buffer = make_shared<RHI_IndexBuffer>(rhi_device, false, "mesh");
        m_index_buffer->Create(m_indices);

        SP_ASSERT_MSG(!m_vertices.empty(), "There are no vertices");
        m_vertex_buffer = make_shared<RHI_VertexBuffer>(rhi_device, false, "mesh");
        m_vertex_buffer->Create(m_vertices);
    }

    void Mesh::AddMaterial(shared_ptr<Material>& material, const shared_ptr<Entity>& entity) const
    {
        SP_ASSERT(material != nullptr);
        SP_ASSERT(entity != nullptr);

        // Create a file path for this material
        const string spartan_asset_path = FileSystem::GetDirectoryFromFilePath(GetResourceFilePathNative()) + material->GetResourceName() + EXTENSION_MATERIAL;
        material->SetResourceFilePath(spartan_asset_path);

        // Create a Renderable and pass the material to it
        entity->AddComponent<Renderable>()->SetMaterial(material);
    }

    void Mesh::AddTexture(shared_ptr<Material>& material, const MaterialTexture texture_type, const string& file_path, bool is_gltf)
    {
        SP_ASSERT(material != nullptr);
        SP_ASSERT(!file_path.empty());

        // Try to get the texture
        const auto tex_name = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        shared_ptr<RHI_Texture> texture = ResourceCache::GetByName<RHI_Texture2D>(tex_name);

        if (texture)
        {
            material->SetTexture(texture_type, texture);
        }
        else // If we didn't get a texture, it's not cached, hence we have to load it and cache it now
        {
            // Load texture
            texture = ResourceCache::Load<RHI_Texture2D>(file_path, RHI_Texture_Srv | RHI_Texture_Mips | RHI_Texture_PerMipViews | RHI_Texture_Compressed);

            // Set the texture to the provided material
            material->SetTexture(texture_type, texture);
        }
    }
}