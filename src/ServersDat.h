#pragma once

#include <filesystem>
#include <string>

/*
    Minimal Minecraft servers.dat writer.

    NewtTech currently manages one server per pack. The generated NBT contains
    the standard root compound with a "servers" list and one server compound:

        name        = pack name
        ip          = manifest.server.address
        hideAddress = 0

    The file is only created/replaced when a server address is configured.
*/
class ServersDat
{
public:
    static void writeManagedServer(
        const std::filesystem::path& instanceRoot,
        const std::wstring& serverName,
        const std::wstring& serverAddress
    );
};
