#include "testcommon.h"

#include <Windows.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace HTest
{

fs::path GetExecutableDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return fs::path(path).parent_path();
}

std::vector<std::uint8_t> ReadBinaryFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return {};
    }

    const std::streamsize size = file.tellg();
    if (size <= 0)
    {
        return {};
    }

    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(data.data()), size))
    {
        return {};
    }

    return data;
}

int ParseSelfTestDurationMs(int argc, char** argv)
{
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const std::string argument = argv[argumentIndex];
        const std::string prefix = "--self-test-ms=";
        if (argument.rfind(prefix, 0) == 0)
        {
            return std::max(0, std::atoi(argument.substr(prefix.size()).c_str()));
        }

        if (argument == "--self-test")
        {
            return 2000;
        }
    }

    return 0;
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int requiredSize = ::WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);

    if (requiredSize <= 1)
    {
        return {};
    }

    std::vector<char> buffer(static_cast<std::size_t>(requiredSize), '\0');
    ::WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        -1,
        buffer.data(),
        requiredSize,
        nullptr,
        nullptr);

    return std::string(buffer.data());
}

void BackEndMessageCallback::message(HRHI::EMessageSeverity severity, const char* messageText)
{
    const char* text = messageText != nullptr ? messageText : "Unknown backend message.";

    switch (severity)
    {
    case HRHI::EMessageSeverity::Info:
        HApp::ZWConsoleLogger::Info("[HRHI] {}", text);
        break;

    case HRHI::EMessageSeverity::Warning:
        HApp::ZWConsoleLogger::Warning("[HRHI] {}", text);
        break;

    case HRHI::EMessageSeverity::Error:
    case HRHI::EMessageSeverity::Fatal:
        HApp::ZWConsoleLogger::Error("[HRHI] {}", text);
        break;
    }
}

}
