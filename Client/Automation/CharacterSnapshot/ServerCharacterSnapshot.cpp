#include "ServerCharacterSnapshot.h"

#include <Windows.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace
{
    using ServerCharacterSnapshot =
        fable::automation::character_snapshot::ServerCharacterSnapshot;

    class SnapshotJsonCursor final
    {
    public:
        explicit SnapshotJsonCursor(const std::string& content)
            : content_(content)
        {
        }

        void SkipWhitespace()
        {
            while (position_ < content_.size())
            {
                const char value = content_[position_];
                if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
                {
                    break;
                }
                ++position_;
            }
        }

        bool Consume(char expected)
        {
            SkipWhitespace();
            if (position_ >= content_.size() || content_[position_] != expected)
            {
                return false;
            }
            ++position_;
            return true;
        }

        bool AtEnd()
        {
            SkipWhitespace();
            return position_ == content_.size();
        }

        bool ReadString(std::string& value)
        {
            value.clear();
            if (!Consume('"'))
            {
                Fail("expected-json-string");
                return false;
            }

            while (position_ < content_.size())
            {
                const unsigned char character =
                    static_cast<unsigned char>(content_[position_++]);
                if (character == '"')
                {
                    return true;
                }
                if (character < 0x20)
                {
                    Fail("control-character-in-json-string");
                    return false;
                }
                if (character != '\\')
                {
                    value.push_back(static_cast<char>(character));
                    continue;
                }

                if (position_ >= content_.size())
                {
                    Fail("unterminated-json-string-escape");
                    return false;
                }
                switch (content_[position_++])
                {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case 'u':
                    Fail("unicode-escapes-are-not-supported-use-direct-utf8");
                    return false;
                default:
                    Fail("invalid-json-string-escape");
                    return false;
                }
            }

            Fail("unterminated-json-string");
            return false;
        }

        bool ReadNumber(double& value)
        {
            SkipWhitespace();
            if (position_ >= content_.size() ||
                (content_[position_] != '-' &&
                    (content_[position_] < '0' || content_[position_] > '9')))
            {
                Fail("expected-json-number");
                return false;
            }

            const char* const begin = content_.c_str() + position_;
            char* end = nullptr;
            errno = 0;
            value = std::strtod(begin, &end);
            if (end == begin || errno == ERANGE || !std::isfinite(value))
            {
                Fail("invalid-json-number");
                return false;
            }
            position_ = static_cast<std::size_t>(end - content_.c_str());
            return true;
        }

        void Fail(const char* failure)
        {
            if (failure_.empty())
            {
                failure_ = failure;
            }
        }

        const std::string& Failure() const noexcept
        {
            return failure_;
        }

    private:
        const std::string& content_;
        std::size_t position_ = 0;
        std::string failure_;
    };

    bool ParseSpawn(
        SnapshotJsonCursor& cursor,
        ServerCharacterSnapshot& snapshot,
        std::string& failure)
    {
        if (!cursor.Consume('{'))
        {
            failure = "spawn-must-be-an-object";
            return false;
        }

        unsigned int fields = 0;
        bool first = true;
        for (;;)
        {
            if (cursor.Consume('}'))
            {
                break;
            }
            if (!first && !cursor.Consume(','))
            {
                failure = "expected-comma-in-spawn";
                return false;
            }
            first = false;

            std::string name;
            double number = 0.0;
            if (!cursor.ReadString(name) || !cursor.Consume(':') ||
                !cursor.ReadNumber(number))
            {
                failure = cursor.Failure().empty()
                    ? "invalid-spawn-property"
                    : cursor.Failure();
                return false;
            }

            unsigned int bit = 0;
            float* destination = nullptr;
            if (name == "x")
            {
                bit = 1u << 0;
                destination = &snapshot.position[0];
            }
            else if (name == "y")
            {
                bit = 1u << 1;
                destination = &snapshot.position[1];
            }
            else if (name == "z")
            {
                bit = 1u << 2;
                destination = &snapshot.position[2];
            }
            else if (name == "facing")
            {
                bit = 1u << 3;
                destination = &snapshot.facingAngle;
            }
            else
            {
                failure = "unknown-spawn-property";
                return false;
            }

            if ((fields & bit) != 0)
            {
                failure = "duplicate-spawn-property";
                return false;
            }
            fields |= bit;
            *destination = static_cast<float>(number);
        }

        if (fields != 0x0F)
        {
            failure = "spawn-requires-x-y-z-and-facing";
            return false;
        }
        return true;
    }

    bool ParseSnapshot(
        const std::string& content,
        ServerCharacterSnapshot& snapshot,
        std::string& failure)
    {
        SnapshotJsonCursor cursor(content);
        if (!cursor.Consume('{'))
        {
            failure = "snapshot-root-must-be-an-object";
            return false;
        }

        unsigned int fields = 0;
        bool first = true;
        for (;;)
        {
            if (cursor.Consume('}'))
            {
                break;
            }
            if (!first && !cursor.Consume(','))
            {
                failure = "expected-comma-in-snapshot";
                return false;
            }
            first = false;

            std::string name;
            if (!cursor.ReadString(name) || !cursor.Consume(':'))
            {
                failure = cursor.Failure().empty()
                    ? "invalid-snapshot-property"
                    : cursor.Failure();
                return false;
            }

            unsigned int bit = 0;
            if (name == "schema_version")
            {
                bit = 1u << 0;
                double value = 0.0;
                if (!cursor.ReadNumber(value) || value != 1.0)
                {
                    failure = "schema-version-must-be-1";
                    return false;
                }
                snapshot.schemaVersion = 1;
            }
            else if (name == "server_character_id")
            {
                bit = 1u << 1;
                if (!cursor.ReadString(snapshot.serverCharacterId))
                {
                    failure = cursor.Failure();
                    return false;
                }
            }
            else if (name == "display_name")
            {
                bit = 1u << 2;
                if (!cursor.ReadString(snapshot.displayName))
                {
                    failure = cursor.Failure();
                    return false;
                }
            }
            else if (name == "bootstrap_save")
            {
                bit = 1u << 3;
                if (!cursor.ReadString(snapshot.bootstrapSave))
                {
                    failure = cursor.Failure();
                    return false;
                }
            }
            else if (name == "spawn")
            {
                bit = 1u << 4;
                if (!ParseSpawn(cursor, snapshot, failure))
                {
                    return false;
                }
            }
            else if (name == "combat_health")
            {
                bit = 1u << 5;
                double value = 0.0;
                if (!cursor.ReadNumber(value))
                {
                    failure = cursor.Failure();
                    return false;
                }
                snapshot.combatHealth = static_cast<float>(value);
            }
            else if (name == "region_index")
            {
                bit = 1u << 6;
                double value = 0.0;
                if (!cursor.ReadNumber(value) ||
                    value < 1.0 || value > 1'024.0 ||
                    std::floor(value) != value)
                {
                    failure = "region-index-must-be-an-integer-from-1-to-1024";
                    return false;
                }
                snapshot.regionIndex = static_cast<int>(value);
            }
            else
            {
                failure = "unknown-snapshot-property";
                return false;
            }

            if ((fields & bit) != 0)
            {
                failure = "duplicate-snapshot-property";
                return false;
            }
            fields |= bit;
        }

        if (!cursor.AtEnd())
        {
            failure = "trailing-data-after-snapshot";
            return false;
        }
        if (fields != 0x7F)
        {
            failure = "snapshot-is-missing-a-required-property";
            return false;
        }
        if (snapshot.serverCharacterId.empty() ||
            snapshot.serverCharacterId.size() > 96)
        {
            failure = "server-character-id-length-is-invalid";
            return false;
        }
        for (const unsigned char character : snapshot.serverCharacterId)
        {
            const bool allowed =
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '.' || character == '_' ||
                character == ':' || character == '-';
            if (!allowed)
            {
                failure = "server-character-id-contains-an-invalid-character";
                return false;
            }
        }
        if (snapshot.displayName.empty() || snapshot.displayName.size() > 128)
        {
            failure = "display-name-length-is-invalid";
            return false;
        }
        for (const unsigned char character : snapshot.displayName)
        {
            if (character < 0x20)
            {
                failure = "display-name-contains-a-control-character";
                return false;
            }
        }
        if (snapshot.bootstrapSave != "AutoSave")
        {
            failure = "bootstrap-save-must-be-AutoSave";
            return false;
        }
        if (!std::isfinite(snapshot.position[0]) ||
            !std::isfinite(snapshot.position[1]) ||
            !std::isfinite(snapshot.position[2]) ||
            !std::isfinite(snapshot.facingAngle) ||
            !std::isfinite(snapshot.combatHealth) ||
            std::fabs(snapshot.position[0]) > 1'000'000.0f ||
            std::fabs(snapshot.position[1]) > 1'000'000.0f ||
            std::fabs(snapshot.position[2]) > 1'000'000.0f ||
            std::fabs(snapshot.facingAngle) > 10'000.0f ||
            snapshot.combatHealth <= 0.0f ||
            snapshot.combatHealth > 1'000'000.0f)
        {
            failure = "snapshot-state-is-out-of-range";
            return false;
        }
        return true;
    }

    bool ReadSmallFile(
        const wchar_t* path,
        std::string& content,
        std::string& failure)
    {
        const HANDLE file = CreateFileW(
            path,
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            failure = "character-snapshot-open-failed";
            return false;
        }

        LARGE_INTEGER size = {};
        if (!GetFileSizeEx(file, &size) ||
            size.QuadPart <= 0 ||
            size.QuadPart > 65'536)
        {
            CloseHandle(file);
            failure = "character-snapshot-size-is-invalid";
            return false;
        }

        content.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD bytesRead = 0;
        const bool read = ReadFile(
            file,
            content.data(),
            static_cast<DWORD>(content.size()),
            &bytesRead,
            nullptr) != FALSE;
        CloseHandle(file);
        if (!read || bytesRead != content.size())
        {
            failure = "character-snapshot-read-failed";
            return false;
        }
        if (content.size() >= 3 &&
            static_cast<unsigned char>(content[0]) == 0xEF &&
            static_cast<unsigned char>(content[1]) == 0xBB &&
            static_cast<unsigned char>(content[2]) == 0xBF)
        {
            content.erase(0, 3);
        }
        return true;
    }
}

namespace fable::automation::character_snapshot
{
    bool ServerCharacterSnapshotLoader::Load(
        const wchar_t* path,
        ServerCharacterSnapshot& snapshot,
        std::string& failure)
    {
        snapshot = {};
        failure.clear();
        if (path == nullptr || path[0] == L'\0')
        {
            failure = "character-snapshot-path-is-empty";
            return false;
        }

        std::string content;
        return ReadSmallFile(path, content, failure) &&
            ParseSnapshot(content, snapshot, failure);
    }
}
