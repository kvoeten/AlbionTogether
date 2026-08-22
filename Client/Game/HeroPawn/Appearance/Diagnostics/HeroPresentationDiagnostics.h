#pragma once

#include "Core/Diagnostics/Diagnostics.h"

namespace fable::game::hero_pawn::appearance
{
    bool ReportHeroSkeletalPresentation(
        const char* label,
        void* nativeThing,
        const core::Diagnostics& diagnostics);
}
