#pragma once

#include "Core/Diagnostics/Diagnostics.h"

namespace fable::multiplayer::presentation
{
    bool ReportHeroSkeletalPresentation(
        const char* label,
        void* nativeThing,
        const core::Diagnostics& diagnostics);
}
