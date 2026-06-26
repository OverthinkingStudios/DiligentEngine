#pragma once

namespace Diligent
{

class InputControllerLinux : public InputControllerBase
{
public:
    ~InputControllerLinux();

    int HandleXEvent(void* xevent);
    int HandleXCBEvent(void* xcb_event);

    void InitXCBKeysms(void* connection);

private:
    int HandleKeyEvevnt(unsigned int keysym, bool IsKeyPressed);

    void* m_XCBKeySymbols = nullptr;
};

} // namespace Diligent
