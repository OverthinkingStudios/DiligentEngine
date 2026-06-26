#pragma once

namespace Diligent
{

class InputControllerWin32 : public InputControllerBase
{
public:
    InputControllerWin32();

    bool HandleNativeMessage(const void* MsgData);

    const MouseState& GetMouseState();

private:
    void UpdateMousePos();
};

} // namespace Diligent
