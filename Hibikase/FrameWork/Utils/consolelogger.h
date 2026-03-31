#pragma once

#include <spdlog/spdlog.h>

#include <cstddef>
#include <string_view>
#include <utility>

namespace HApp
{

class ZWConsoleLogger final
{
public:
    static void Initialize();
    static void Shutdown();

    static void PrintBanner(std::string_view title);
    static void PrintSection(std::string_view title);
    static void PrintProperty(std::string_view label, const char* value);
    static void PrintProperty(std::string_view label, std::string_view value);
    static void PrintProperty(std::string_view label, bool value);
    static void PrintProperty(std::string_view label, std::size_t value);
    static void PrintListItem(std::string_view item);

    template <typename... Args>
    static void Trace(spdlog::format_string_t<Args...> format, Args&&... args)
    {
        GetLogger().trace(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Debug(spdlog::format_string_t<Args...> format, Args&&... args)
    {
        GetLogger().debug(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Info(spdlog::format_string_t<Args...> format, Args&&... args)
    {
        GetLogger().info(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Warning(spdlog::format_string_t<Args...> format, Args&&... args)
    {
        GetLogger().warn(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Error(spdlog::format_string_t<Args...> format, Args&&... args)
    {
        GetLogger().error(format, std::forward<Args>(args)...);
    }

private:
    static spdlog::logger& GetLogger();
};

}
