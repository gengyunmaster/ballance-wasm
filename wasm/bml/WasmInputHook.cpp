#include "BML/InputHook.h"

#include <cassert>
#include <cstring>
#include <unordered_map>

struct InputHook::Impl {
    explicit Impl(CKInputManager *manager) : Input(manager) {}

    CKInputManager *Input = nullptr;
    unsigned char LastKeyboard[256] = {};
    Vx2DVector LastMouse;
    int Blocked[CK_INPUT_DEVICE_COUNT] = {};
    uint64_t NextToken = 1;
    std::unordered_map<uint64_t, uint32_t> Tokens;

    bool IsBlocked(CK_INPUT_DEVICE device) const {
        return device >= 0 && device < CK_INPUT_DEVICE_COUNT && Blocked[device] > 0;
    }

    void Block(CK_INPUT_DEVICE device) {
        if (device >= 0 && device < CK_INPUT_DEVICE_COUNT)
            ++Blocked[device];
    }

    void Unblock(CK_INPUT_DEVICE device) {
        if (device >= 0 && device < CK_INPUT_DEVICE_COUNT && Blocked[device] > 0)
            --Blocked[device];
    }

    void ApplyMask(uint32_t mask, bool block) {
        for (int device = 0; device < CK_INPUT_DEVICE_COUNT; ++device) {
            if (mask & (1u << device)) {
                if (block)
                    Block(static_cast<CK_INPUT_DEVICE>(device));
                else
                    Unblock(static_cast<CK_INPUT_DEVICE>(device));
            }
        }
    }
};

InputHook::InputHook(CKInputManager *input) : m_Impl(new Impl(input)) {
    assert(input != nullptr);
}

InputHook::~InputHook() {
    delete m_Impl;
}

void InputHook::EnableKeyboardRepetition(CKBOOL enable) { m_Impl->Input->EnableKeyboardRepetition(enable); }
CKBOOL InputHook::IsKeyboardRepetitionEnabled() { return m_Impl->Input->IsKeyboardRepetitionEnabled(); }
CKBOOL InputHook::IsKeyDown(CKDWORD key, CKDWORD *stamp) { return oIsKeyDown(key, stamp) && !m_Impl->IsBlocked(CK_INPUT_DEVICE_KEYBOARD); }
CKBOOL InputHook::IsKeyUp(CKDWORD key) { return oIsKeyUp(key) && !m_Impl->IsBlocked(CK_INPUT_DEVICE_KEYBOARD); }
CKBOOL InputHook::IsKeyToggled(CKDWORD key, CKDWORD *stamp) { return oIsKeyToggled(key, stamp) && !m_Impl->IsBlocked(CK_INPUT_DEVICE_KEYBOARD); }
void InputHook::GetKeyName(CKDWORD key, CKSTRING name) { m_Impl->Input->GetKeyName(key, const_cast<char *>(name)); }
CKDWORD InputHook::GetKeyFromName(CKSTRING name) { return m_Impl->Input->GetKeyFromName(name); }
unsigned char *InputHook::GetKeyboardState() {
    static unsigned char empty[256] = {};
    return m_Impl->IsBlocked(CK_INPUT_DEVICE_KEYBOARD) ? empty : oGetKeyboardState();
}
CKBOOL InputHook::IsKeyboardAttached() { return m_Impl->Input->IsKeyboardAttached(); }
int InputHook::GetNumberOfKeyInBuffer() { return m_Impl->IsBlocked(CK_INPUT_DEVICE_KEYBOARD) ? 0 : oGetNumberOfKeyInBuffer(); }
int InputHook::GetKeyFromBuffer(int index, CKDWORD &key, CKDWORD *stamp) {
    return m_Impl->IsBlocked(CK_INPUT_DEVICE_KEYBOARD) ? NO_KEY : oGetKeyFromBuffer(index, key, stamp);
}
CKBOOL InputHook::IsMouseButtonDown(CK_MOUSEBUTTON button) { return oIsMouseButtonDown(button) && !m_Impl->IsBlocked(CK_INPUT_DEVICE_MOUSE); }
CKBOOL InputHook::IsMouseClicked(CK_MOUSEBUTTON button) { return oIsMouseClicked(button) && !m_Impl->IsBlocked(CK_INPUT_DEVICE_MOUSE); }
CKBOOL InputHook::IsMouseToggled(CK_MOUSEBUTTON button) { return oIsMouseToggled(button) && !m_Impl->IsBlocked(CK_INPUT_DEVICE_MOUSE); }
void InputHook::GetMouseButtonsState(CKBYTE states[4]) {
    if (m_Impl->IsBlocked(CK_INPUT_DEVICE_MOUSE))
        std::memset(states, KS_IDLE, sizeof(CKBYTE) * 4);
    else
        oGetMouseButtonsState(states);
}
void InputHook::GetMousePosition(Vx2DVector &position, CKBOOL absolute) {
    if (!m_Impl->IsBlocked(CK_INPUT_DEVICE_MOUSE))
        m_Impl->Input->GetMousePosition(position, absolute);
}
void InputHook::GetMouseRelativePosition(VxVector &position) {
    if (!m_Impl->IsBlocked(CK_INPUT_DEVICE_MOUSE))
        m_Impl->Input->GetMouseRelativePosition(position);
}
void InputHook::GetLastMousePosition(Vx2DVector &position) { position = m_Impl->LastMouse; }
CKBOOL InputHook::IsMouseAttached() { return m_Impl->Input->IsMouseAttached(); }
CKBOOL InputHook::IsJoystickAttached(int joystick) { return m_Impl->Input->IsJoystickAttached(joystick); }
void InputHook::GetJoystickPosition(int joystick, VxVector *position) {
    if (!m_Impl->IsBlocked(CK_INPUT_DEVICE_JOYSTICK)) m_Impl->Input->GetJoystickPosition(joystick, position);
}
void InputHook::GetJoystickRotation(int joystick, VxVector *rotation) {
    if (!m_Impl->IsBlocked(CK_INPUT_DEVICE_JOYSTICK)) m_Impl->Input->GetJoystickRotation(joystick, rotation);
}
void InputHook::GetJoystickSliders(int joystick, Vx2DVector *position) {
    if (!m_Impl->IsBlocked(CK_INPUT_DEVICE_JOYSTICK)) m_Impl->Input->GetJoystickSliders(joystick, position);
}
void InputHook::GetJoystickPointOfViewAngle(int joystick, float *angle) {
    if (!m_Impl->IsBlocked(CK_INPUT_DEVICE_JOYSTICK)) m_Impl->Input->GetJoystickPointOfViewAngle(joystick, angle);
}
CKDWORD InputHook::GetJoystickButtonsState(int joystick) {
    return m_Impl->IsBlocked(CK_INPUT_DEVICE_JOYSTICK) ? 0 : m_Impl->Input->GetJoystickButtonsState(joystick);
}
CKBOOL InputHook::IsJoystickButtonDown(int joystick, int button) {
    return !m_Impl->IsBlocked(CK_INPUT_DEVICE_JOYSTICK) && m_Impl->Input->IsJoystickButtonDown(joystick, button);
}
void InputHook::Pause(CKBOOL pause) { m_Impl->Input->Pause(pause); }
void InputHook::ShowCursor(CKBOOL show) { m_Impl->Input->ShowCursor(show); }
CKBOOL InputHook::GetCursorVisibility() { return m_Impl->Input->GetCursorVisibility(); }
VXCURSOR_POINTER InputHook::GetSystemCursor() { return m_Impl->Input->GetSystemCursor(); }
void InputHook::SetSystemCursor(VXCURSOR_POINTER cursor) { m_Impl->Input->SetSystemCursor(cursor); }

CKBOOL InputHook::IsKeyPressed(CKDWORD key) {
    return !m_Impl->IsBlocked(CK_INPUT_DEVICE_KEYBOARD) && oIsKeyPressed(key);
}
CKBOOL InputHook::IsKeyReleased(CKDWORD key) {
    return !m_Impl->IsBlocked(CK_INPUT_DEVICE_KEYBOARD) && oIsKeyReleased(key);
}
CKBOOL InputHook::oIsKeyPressed(CKDWORD key) {
    return key < 256 && oIsKeyDown(key) && !m_Impl->LastKeyboard[key];
}
CKBOOL InputHook::oIsKeyReleased(CKDWORD key) {
    return key < 256 && oIsKeyToggled(key) && m_Impl->LastKeyboard[key];
}
CKBOOL InputHook::oIsKeyDown(CKDWORD key, CKDWORD *stamp) { return m_Impl->Input->IsKeyDown(key, stamp); }
CKBOOL InputHook::oIsKeyUp(CKDWORD key) { return m_Impl->Input->IsKeyUp(key); }
CKBOOL InputHook::oIsKeyToggled(CKDWORD key, CKDWORD *stamp) { return m_Impl->Input->IsKeyToggled(key, stamp); }
unsigned char *InputHook::oGetKeyboardState() { return m_Impl->Input->GetKeyboardState(); }
int InputHook::oGetNumberOfKeyInBuffer() { return m_Impl->Input->GetNumberOfKeyInBuffer(); }
int InputHook::oGetKeyFromBuffer(int index, CKDWORD &key, CKDWORD *stamp) { return m_Impl->Input->GetKeyFromBuffer(index, key, stamp); }
CKBOOL InputHook::oIsMouseButtonDown(CK_MOUSEBUTTON button) { return m_Impl->Input->IsMouseButtonDown(button); }
CKBOOL InputHook::oIsMouseClicked(CK_MOUSEBUTTON button) { return m_Impl->Input->IsMouseClicked(button); }
CKBOOL InputHook::oIsMouseToggled(CK_MOUSEBUTTON button) { return m_Impl->Input->IsMouseToggled(button); }
void InputHook::oGetMouseButtonsState(CKBYTE states[4]) { m_Impl->Input->GetMouseButtonsState(states); }

bool InputHook::IsBlock() { return m_Impl->IsBlocked(CK_INPUT_DEVICE_KEYBOARD); }
void InputHook::SetBlock(bool block) { block ? m_Impl->Block(CK_INPUT_DEVICE_KEYBOARD) : m_Impl->Unblock(CK_INPUT_DEVICE_KEYBOARD); }
int InputHook::IsBlocked(CK_INPUT_DEVICE device) {
    return device >= 0 && device < CK_INPUT_DEVICE_COUNT ? m_Impl->Blocked[device] : 0;
}
void InputHook::Block(CK_INPUT_DEVICE device) { m_Impl->Block(device); }
void InputHook::Unblock(CK_INPUT_DEVICE device) { m_Impl->Unblock(device); }
uint64_t InputHook::AcquireBlock(uint32_t mask) {
    mask &= INPUT_BLOCK_ALL;
    if (!mask)
        return 0;
    const uint64_t token = m_Impl->NextToken++;
    if (m_Impl->NextToken == 0)
        m_Impl->NextToken = 1;
    m_Impl->Tokens[token] = mask;
    m_Impl->ApplyMask(mask, true);
    return token;
}
void InputHook::ReleaseBlock(uint64_t token) {
    const auto found = m_Impl->Tokens.find(token);
    if (found == m_Impl->Tokens.end())
        return;
    m_Impl->ApplyMask(found->second, false);
    m_Impl->Tokens.erase(found);
}
void InputHook::Process() {
    if (!m_Impl || !m_Impl->Input)
        return;
    m_Impl->Input->PostProcess();
    const unsigned char *keyboard = oGetKeyboardState();
    if (keyboard)
        std::memcpy(m_Impl->LastKeyboard, keyboard, sizeof(m_Impl->LastKeyboard));
    m_Impl->Input->GetMousePosition(m_Impl->LastMouse, FALSE);
}
