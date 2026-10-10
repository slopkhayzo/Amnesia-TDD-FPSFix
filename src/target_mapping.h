#pragma once

#include <Windows.h>

#include <string>

namespace atdd::target {

struct Mapping {
    void* engineRun = nullptr;
    void* updaterRunMessage = nullptr;
    void* sceneRender = nullptr;
    void* cameraSetPosition = nullptr;
    void* cameraSetPitch = nullptr;
    void* cameraSetYaw = nullptr;
    void* cameraSetRoll = nullptr;
    void* cameraAddPitch = nullptr;
    void* cameraAddYaw = nullptr;
    void* luxPlayerAddYaw = nullptr;
    void* luxPlayerAddPitch = nullptr;
    void* playerHandsPostUpdate = nullptr;
    void* luxPropOnUpdate = nullptr;
    void* luxEnemyOnUpdate = nullptr;
    void* entityGetWorldMatrix = nullptr;
    void* entityGetEntityParent = nullptr;
    void* physicsBodyGetEnabled = nullptr;
    void* newtonTransformCallback = nullptr;
    void* newtonBodyGetUserData = nullptr;
    void* meshEntityUpdateGraphics = nullptr;
    void* particleEmitterUpdateGraphics = nullptr;
    void* imageTrailRenderEffect = nullptr;
    void* subMeshGetModelMatrix = nullptr;
    void* mouseSdlUpdate = nullptr;
    void* mouseSdlGetRelPosition = nullptr;
    void* guiSendMousePos = nullptr;
    void* sdlGetRelativeMouseState = nullptr;
    void* sdlPumpEvents = nullptr;
    void* sdlGetTicks = nullptr;
    void* luxUpdateGamePlayerInput = nullptr;
};

[[nodiscard]] bool ResolveAndValidate(HMODULE executable,
                                      Mapping& mapping,
                                      std::wstring& error);

} // namespace atdd::target
