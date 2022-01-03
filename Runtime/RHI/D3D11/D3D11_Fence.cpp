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

//= INCLUDES =====================
#include "Spartan.h"
#include "../RHI_Fence.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
//================================

namespace Spartan
{
    RHI_Fence::RHI_Fence(RHI_Device* rhi_device, const char* name /*= nullptr*/)
    {

    }

    RHI_Fence::~RHI_Fence()
    {

    }

    bool RHI_Fence::IsSignaled()
    {
        return true;
    }

    bool RHI_Fence::Wait(uint64_t timeout /*= std::numeric_limits<uint64_t>::max()*/)
    {
        return true;
    }

    bool RHI_Fence::Reset()
    {
        return true;
    }
}
