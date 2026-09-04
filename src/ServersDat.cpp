#include <windows.h>

#include "ServersDat.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    // Minecraft NBT strings are UTF-8 and use a big-endian unsigned-short length.
    std::string wideToUtf8(const std::wstring& value)
    {
        if (value.empty())
            return {};

        int size =
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.c_str(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr
            );

        if (size <= 0)
            throw std::runtime_error(
                "Unable to encode servers.dat text as UTF-8."
            );

        std::string result(
            static_cast<size_t>(size),
            '\0'
        );

        WideCharToMultiByte(
            CP_UTF8,
            0,
            value.c_str(),
            static_cast<int>(value.size()),
            result.data(),
            size,
            nullptr,
            nullptr
        );

        return result;
    }

    void writeU16(
        std::ofstream& output,
        std::uint16_t value)
    {
        const unsigned char bytes[2] = {
            static_cast<unsigned char>(
                (value >> 8) & 0xFF
            ),
            static_cast<unsigned char>(
                value & 0xFF
            )
        };

        output.write(
            reinterpret_cast<const char*>(bytes),
            2
        );
    }

    void writeI32(
        std::ofstream& output,
        std::int32_t value)
    {
        const std::uint32_t raw =
            static_cast<std::uint32_t>(value);

        const unsigned char bytes[4] = {
            static_cast<unsigned char>(
                (raw >> 24) & 0xFF
            ),
            static_cast<unsigned char>(
                (raw >> 16) & 0xFF
            ),
            static_cast<unsigned char>(
                (raw >> 8) & 0xFF
            ),
            static_cast<unsigned char>(
                raw & 0xFF
            )
        };

        output.write(
            reinterpret_cast<const char*>(bytes),
            4
        );
    }

    void writeStringPayload(
        std::ofstream& output,
        const std::string& value)
    {
        if (value.size() > 65535)
            throw std::runtime_error(
                "servers.dat string is too long."
            );

        writeU16(
            output,
            static_cast<std::uint16_t>(
                value.size()
            )
        );

        output.write(
            value.data(),
            static_cast<std::streamsize>(
                value.size()
            )
        );
    }

    void writeNamedString(
        std::ofstream& output,
        const char* tagName,
        const std::string& value)
    {
        // TAG_String
        output.put(static_cast<char>(8));
        writeStringPayload(output, tagName);
        writeStringPayload(output, value);
    }

    void writeNamedByte(
        std::ofstream& output,
        const char* tagName,
        unsigned char value)
    {
        // TAG_Byte
        output.put(static_cast<char>(1));
        writeStringPayload(output, tagName);
        output.put(static_cast<char>(value));
    }
}

void ServersDat::writeManagedServer(
    const std::filesystem::path& instanceRoot,
    const std::wstring& serverName,
    const std::wstring& serverAddress)
{
    if (serverAddress.empty())
        return;

    std::filesystem::create_directories(
        instanceRoot
    );

    const std::filesystem::path target =
        instanceRoot / "servers.dat";

    const std::filesystem::path temp =
        instanceRoot / "servers.dat.newttech.tmp";

    std::ofstream output(
        temp,
        std::ios::binary |
        std::ios::trunc
    );

    if (!output)
        throw std::runtime_error(
            "Unable to create Minecraft servers.dat."
        );

    const std::string name =
        wideToUtf8(
            serverName.empty()
                ? L"NewtTech Server"
                : serverName
        );

    const std::string address =
        wideToUtf8(serverAddress);

    /*
        Root:
          TAG_Compound("")
            TAG_List("servers", TAG_Compound, 1)
              TAG_Compound
                TAG_String("name", pack name)
                TAG_String("ip", address)
                TAG_Byte("hideAddress", 0)
                TAG_End
            TAG_End
    */

    // Root TAG_Compound with an empty name.
    output.put(static_cast<char>(10));
    writeStringPayload(output, "");

    // TAG_List "servers"
    output.put(static_cast<char>(9));
    writeStringPayload(output, "servers");

    // List element type: TAG_Compound
    output.put(static_cast<char>(10));

    // One managed server.
    writeI32(output, 1);

    writeNamedString(
        output,
        "name",
        name
    );

    writeNamedString(
        output,
        "ip",
        address
    );

    writeNamedByte(
        output,
        "hideAddress",
        0
    );

    // End server compound.
    output.put(static_cast<char>(0));

    // End root compound.
    output.put(static_cast<char>(0));

    output.flush();

    if (!output)
    {
        output.close();
        std::filesystem::remove(temp);

        throw std::runtime_error(
            "Unable to finish writing Minecraft servers.dat."
        );
    }

    output.close();

    /*
        This first implementation intentionally treats the pack's server list as
        managed data. Each NewtTech pack has one associated server, so install
        and Verify/Repair deterministically restore that server.
    */
    if (std::filesystem::exists(target))
        std::filesystem::remove(target);

    std::filesystem::rename(
        temp,
        target
    );
}
