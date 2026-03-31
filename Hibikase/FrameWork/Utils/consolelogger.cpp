#include <Utils\consolelogger.h>

#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>
#include <string>

namespace
{

constexpr std::size_t kConsoleLineWidth = 68;

std::shared_ptr<spdlog::logger> gConsoleLogger;

std::string MakeRule(char fill)
{
    return std::string(kConsoleLineWidth, fill);
}

std::string MakeLabel(std::string_view label)
{
    return fmt::format("{:<28}", fmt::format("{}:", label));
}

}

namespace HApp
{

void ZWConsoleLogger::Initialize()
{
    if (gConsoleLogger != nullptr)
    {
        return;
    }

    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    gConsoleLogger = std::make_shared<spdlog::logger>("hibikase-console", sink);
    gConsoleLogger->set_level(spdlog::level::trace);
    gConsoleLogger->flush_on(spdlog::level::trace);
    gConsoleLogger->set_pattern("%^[%H:%M:%S] [%l] %v%$");
}

void ZWConsoleLogger::Shutdown()
{
    gConsoleLogger.reset();
    spdlog::shutdown();
}

void ZWConsoleLogger::PrintBanner(std::string_view title)
{
    Debug("{}", MakeRule('='));
    Debug("{:^68}", title);
    Debug("{}", MakeRule('='));
}

void ZWConsoleLogger::PrintSection(std::string_view title)
{
    Debug("{:-^68}", fmt::format(" {} ", title));
}

void ZWConsoleLogger::PrintProperty(std::string_view label, const char* value)
{
    PrintProperty(label, std::string_view(value != nullptr ? value : "unknown"));
}

void ZWConsoleLogger::PrintProperty(std::string_view label, std::string_view value)
{
    Info("{} {}", MakeLabel(label), value);
}

void ZWConsoleLogger::PrintProperty(std::string_view label, bool value)
{
    PrintProperty(label, std::string_view(value ? "yes" : "no"));
}

void ZWConsoleLogger::PrintProperty(std::string_view label, std::size_t value)
{
    Info("{} {}", MakeLabel(label), value);
}

void ZWConsoleLogger::PrintListItem(std::string_view item)
{
    Trace("  - {}", item);
}

spdlog::logger& ZWConsoleLogger::GetLogger()
{
    if (gConsoleLogger == nullptr)
    {
        Initialize();
    }

    return *gConsoleLogger;
}

}
