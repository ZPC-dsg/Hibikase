#pragma once

#include <Window\window.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace HApp
{

class ZWWindowManager final
{
public:
    ZWWindowManager();
    ~ZWWindowManager();

    ZWWindowManager(const ZWWindowManager&) = delete;
    ZWWindowManager& operator=(const ZWWindowManager&) = delete;
    ZWWindowManager(ZWWindowManager&&) = delete;
    ZWWindowManager& operator=(ZWWindowManager&&) = delete;

    bool Initialize();
    void Shutdown();

    ZWWindow* CreateWindow(const ZWWindowDesc& windowDesc);
    void DestroyWindow(std::uint32_t windowId);
    void DestroyClosedWindows();

    void PollEvents() const;

    bool HasOpenWindows() const;
    bool IsVulkanSupported() const;
    std::size_t GetWindowCount() const;
    const std::vector<std::unique_ptr<ZWWindow>>& GetWindows() const;
    ZWWindow* GetPrimaryWindow() const;
    ZWWindow* FindWindowById(std::uint32_t windowId) const;
    std::vector<const char*> GetRequiredVulkanExtensions() const;
    const std::string& GetLastError() const;

private:
    static void ErrorCallback(int errorCode, const char* description);

private:
    bool mInitialized{ false };
    bool mVulkanSupported{ false };
    std::uint32_t mNextWindowId{ 1 };
    std::vector<std::unique_ptr<ZWWindow>> mWindows;
    std::string mLastError;
};

}
