/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES =================
#include "pch.h"
#include "../RHI/RHI_Vertex.h"
//============================

namespace Spartan::Math
{
    const BoundingBox BoundingBox::Undefined(Vector3::Infinity, Vector3::InfinityNeg);

    BoundingBox::BoundingBox()
    {
        m_min = Vector3::Infinity;
        m_max = Vector3::InfinityNeg;
    }

    BoundingBox::BoundingBox(const Vector3& min, const Vector3& max)
    {
        this->m_min = min;
        this->m_max = max;
    }

    BoundingBox::BoundingBox(const Vector3* points, const uint32_t point_count)
    {
        m_min = Vector3::Infinity;
        m_max = Vector3::InfinityNeg;

        for (uint32_t i = 0; i < point_count; i++)
        {
            m_max.x = Helper::Max(m_max.x, points[i].x);
            m_max.y = Helper::Max(m_max.y, points[i].y);
            m_max.z = Helper::Max(m_max.z, points[i].z);

            m_min.x = Helper::Min(m_min.x, points[i].x);
            m_min.y = Helper::Min(m_min.y, points[i].y);
            m_min.z = Helper::Min(m_min.z, points[i].z);
        }
    }

    BoundingBox::BoundingBox(const RHI_Vertex_PosTexNorTan* vertices, const uint32_t vertex_count)
    {
        m_min = Vector3::Infinity;
        m_max = Vector3::InfinityNeg;

        for (uint32_t i = 0; i < vertex_count; i++)
        {
            m_max.x = Helper::Max(m_max.x, vertices[i].pos[0]);
            m_max.y = Helper::Max(m_max.y, vertices[i].pos[1]);
            m_max.z = Helper::Max(m_max.z, vertices[i].pos[2]);

            m_min.x = Helper::Min(m_min.x, vertices[i].pos[0]);
            m_min.y = Helper::Min(m_min.y, vertices[i].pos[1]);
            m_min.z = Helper::Min(m_min.z, vertices[i].pos[2]);
        }
    }

    Intersection BoundingBox::Intersects(const Vector3& point) const
    {
        if (point.x < m_min.x || point.x > m_max.x ||
            point.y < m_min.y || point.y > m_max.y ||
            point.z < m_min.z || point.z > m_max.z)
        {
            return Intersection::Outside;
        }
        else
        {
            return Intersection::Inside;
        }
    }

    Intersection BoundingBox::Intersects(const BoundingBox& box) const
    {
        if (box.m_max.x < m_min.x || box.m_min.x > m_max.x ||
            box.m_max.y < m_min.y || box.m_min.y > m_max.y ||
            box.m_max.z < m_min.z || box.m_min.z > m_max.z)
        {
            return Intersection::Outside;
        }
        else if (
                box.m_min.x < m_min.x || box.m_max.x > m_max.x ||
                box.m_min.y < m_min.y || box.m_max.y > m_max.y ||
                box.m_min.z < m_min.z || box.m_max.z > m_max.z)
        {
            return Intersection::Intersects;
        }
        else
        {
            return Intersection::Inside;
        }
    }

    BoundingBox BoundingBox::Transform(const Matrix& transform) const
    {
        const Vector3 center_new = transform * GetCenter();
        const Vector3 extent_old = GetExtents();
        const Vector3 extend_new = Vector3
        (
            Helper::Abs(transform.m00) * extent_old.x + Helper::Abs(transform.m10) * extent_old.y + Helper::Abs(transform.m20) * extent_old.z,
            Helper::Abs(transform.m01) * extent_old.x + Helper::Abs(transform.m11) * extent_old.y + Helper::Abs(transform.m21) * extent_old.z,
            Helper::Abs(transform.m02) * extent_old.x + Helper::Abs(transform.m12) * extent_old.y + Helper::Abs(transform.m22) * extent_old.z
        );

        return BoundingBox(center_new - extend_new, center_new + extend_new);
    }

    void BoundingBox::Merge(const BoundingBox& box)
    {
        m_min.x = Helper::Min(m_min.x, box.m_min.x);
        m_min.y = Helper::Min(m_min.y, box.m_min.y);
        m_min.z = Helper::Min(m_min.z, box.m_min.z);

        m_max.x = Helper::Max(m_max.x, box.m_max.x);
        m_max.y = Helper::Max(m_max.y, box.m_max.y);
        m_max.z = Helper::Max(m_max.z, box.m_max.z);
    }

	bool BoundingBox::Occluded(const BoundingBox& occluder) const
	{
        // lambda for getting a corner of the bounding box
        auto get_corner = [this](uint32_t index) -> Vector3
        {
            return Vector3(
                index & 1 ? m_max.x : m_min.x,
                index & 2 ? m_max.y : m_min.y,
                index & 4 ? m_max.z : m_min.z
            );
        };

        // lambda for checking if a point is behind all planes of the bounding box
        auto is_point_behind_all_planes = [&occluder](const Vector3& point) -> bool
        {
            return (point.x <= occluder.m_max.x && point.x >= occluder.m_min.x &&
                    point.y <= occluder.m_max.y && point.y >= occluder.m_min.y &&
                    point.z <= occluder.m_max.z && point.z >= occluder.m_min.z);
        };

        // check if all corners of this box are behind all the planes of the occluder
        for (uint32_t i = 0; i < 8; i++)
        {
            if (!is_point_behind_all_planes(get_corner(i)))
                return false;
        }

        return true;
	}
}
