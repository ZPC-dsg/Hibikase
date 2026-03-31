#pragma once

#include <Window\windowtypes.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace HApp
{

class ZWInputMapping final
{
public:
    void BindKey(const std::string& actionName, int keyCode);
    void BindMouseButton(const std::string& actionName, int mouseButton);
    bool RemoveBinding(const std::string& actionName, const ZWInputBinding& binding);
    void UnbindAction(const std::string& actionName);
    void Clear();
    bool HasAction(const std::string& actionName) const;
    const std::vector<ZWInputBinding>& GetBindings(const std::string& actionName) const;
    std::vector<std::string> GetActions() const;
    void SetDefaultMovementBindings();

private:
    void AppendBinding(const std::string& actionName, const ZWInputBinding& binding);

private:
    std::unordered_map<std::string, std::vector<ZWInputBinding>> mBindings;
};

}
