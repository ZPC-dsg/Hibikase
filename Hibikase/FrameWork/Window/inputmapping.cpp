#include <Window\inputmapping.h>

#include <algorithm>

namespace HApp
{

void ZWInputMapping::BindKey(const std::string& actionName, int keyCode)
{
    AppendBinding(actionName, ZWInputBinding{ ZWInputBindingType::Key, keyCode });
}

void ZWInputMapping::BindMouseButton(const std::string& actionName, int mouseButton)
{
    AppendBinding(actionName, ZWInputBinding{ ZWInputBindingType::MouseButton, mouseButton });
}

bool ZWInputMapping::RemoveBinding(const std::string& actionName, const ZWInputBinding& binding)
{
    const auto mappingIt = mBindings.find(actionName);
    if (mappingIt == mBindings.end())
    {
        return false;
    }

    std::vector<ZWInputBinding>& bindings = mappingIt->second;
    const auto bindingIt = std::find(bindings.begin(), bindings.end(), binding);
    if (bindingIt == bindings.end())
    {
        return false;
    }

    bindings.erase(bindingIt);
    if (bindings.empty())
    {
        mBindings.erase(mappingIt);
    }

    return true;
}

void ZWInputMapping::UnbindAction(const std::string& actionName)
{
    mBindings.erase(actionName);
}

void ZWInputMapping::Clear()
{
    mBindings.clear();
}

bool ZWInputMapping::HasAction(const std::string& actionName) const
{
    return mBindings.contains(actionName);
}

const std::vector<ZWInputBinding>& ZWInputMapping::GetBindings(const std::string& actionName) const
{
    static const std::vector<ZWInputBinding> emptyBindings;

    const auto mappingIt = mBindings.find(actionName);
    return mappingIt != mBindings.end() ? mappingIt->second : emptyBindings;
}

std::vector<std::string> ZWInputMapping::GetActions() const
{
    std::vector<std::string> actions;
    actions.reserve(mBindings.size());
    for (const auto& bindingEntry : mBindings)
    {
        actions.push_back(bindingEntry.first);
    }

    return actions;
}

void ZWInputMapping::SetDefaultMovementBindings()
{
    Clear();
    BindKey("MoveForward", GLFW_KEY_W);
    BindKey("MoveForward", GLFW_KEY_UP);
    BindKey("MoveBackward", GLFW_KEY_S);
    BindKey("MoveBackward", GLFW_KEY_DOWN);
    BindKey("MoveLeft", GLFW_KEY_A);
    BindKey("MoveLeft", GLFW_KEY_LEFT);
    BindKey("MoveRight", GLFW_KEY_D);
    BindKey("MoveRight", GLFW_KEY_RIGHT);
    BindMouseButton("LookAround", GLFW_MOUSE_BUTTON_RIGHT);
}

void ZWInputMapping::AppendBinding(const std::string& actionName, const ZWInputBinding& binding)
{
    std::vector<ZWInputBinding>& bindings = mBindings[actionName];
    if (std::find(bindings.begin(), bindings.end(), binding) == bindings.end())
    {
        bindings.push_back(binding);
    }
}

}
