#pragma once

#include <BackEnd/RHIinterface.h>
#include <Utils/consolelogger.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace HTest
{

namespace fs = std::filesystem;

fs::path GetExecutableDirectory();
std::vector<std::uint8_t> ReadBinaryFile(const fs::path& path);
int ParseSelfTestDurationMs(int argc, char** argv);
std::string WideToUtf8(const std::wstring& value);

class BackEndMessageCallback final : public HRHI::IMessageCallback
{
public:
    void message(HRHI::EMessageSeverity severity, const char* messageText) override;
};

}
