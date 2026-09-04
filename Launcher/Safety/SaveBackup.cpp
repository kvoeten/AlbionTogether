#include "SaveBackup.h"

#include "../Configuration/Paths.h"

namespace fable::launcher::safety
{
    SaveBackupReport CreateDefaultFableSaveBackup()
    {
        const std::filesystem::path documents = GetOrdinaryDocumentsPath();
        if (documents.empty())
        {
            SaveBackupReport report;
            report.detail = L"Windows Documents could not be resolved, so a safety backup cannot be guaranteed.";
            return report;
        }

        const std::filesystem::path fableRoot =
            documents / L"My Games" / L"FableHD";
        return CreateSaveBackup(
            fableRoot / L"saves",
            fableRoot / L"AlbionTogether Backups",
            10);
    }
}
