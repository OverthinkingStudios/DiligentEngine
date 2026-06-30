#include "EarthworksEditor.hpp"

EarthworksEditor::EarthworksEditor()
    : Diligent::EarthworksFXApplicationBase("Earthworks Editor", "earthworks-editor", overthinking::Env::Stage::Dev)
{
}

namespace Diligent
{

NativeAppBase* CreateApplication()
{
    return new EarthworksEditor();
}

} // namespace Diligent
