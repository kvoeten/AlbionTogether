#pragma once

#include "SaveBackupCore.h"

namespace fable::launcher::safety
{
    // Fable Anniversary default Steam save location:
    //   %USERPROFILE%\Documents\My Games\FableHD\saves
    // Backups are deliberately kept alongside (not inside) the live save tree:
    //   %USERPROFILE%\Documents\My Games\FableHD\AlbionTogether Backups
    SaveBackupReport CreateDefaultFableSaveBackup();
}
