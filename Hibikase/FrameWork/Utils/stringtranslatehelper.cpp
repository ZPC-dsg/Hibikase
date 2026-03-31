#include <Utils/stringtranslatehelper.h>

namespace HApp
{
    const char* DebugNameToString(const std::string& debugName)
    {
        return debugName.empty() ? "<UNNAMED>" : debugName.c_str();
    }
}