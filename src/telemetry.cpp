#include "telemetry.h"

#include "camera_runtime.h"
#include "enemy_pose_runtime.h"
#include "frame_cadence.h"
#include "hands_runtime.h"
#include "image_trail_timing.h"
#include "logger.h"
#include "motion_trace.h"
#include "mouse_preview_math.h"
#include "particle_interpolation.h"
#include "pose_runtime.h"
#include "presentation_clock.h"
#include "target_mapping.h"
#include "world_runtime.h"

#include <MinHook.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <string>

namespace atdd::telemetry {
namespace {

using UpdaterRunMessage = void(__thiscall*)(void*, int, float);
using SceneRender = void(__thiscall*)(void*, float, std::uint32_t);
using EngineRun = void(__thiscall*)(void*);
using PlayerHandsPostUpdate = void(__thiscall*)(void*, float);
using LuxEntityOnUpdate = void(__thiscall*)(void*, float);
using NewtonTransformCallback = void(__cdecl*)(void*, const float*, int);
using NewtonBodyGetUserData = void*(__cdecl*)(void*);
using SubMeshGetModelMatrix = hands::Matrix*(__thiscall*)(void*, void*);
using ParticleEmitterUpdateGraphics = bool(__thiscall*)(void*, void*, float);
using ImageTrailRenderEffect = void*(__thiscall*)(void*, void*, void*);
struct MouseDelta {
    std::int32_t x;
    std::int32_t y;
};
struct PresentationMousePoll {
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool polled = false;
};
struct MousePreviewDiagnostics {
    std::int64_t pendingX = 0;
    std::int64_t pendingY = 0;
    float historyPitch = 0.0F;
    float historyYaw = 0.0F;
    float pendingPitch = 0.0F;
    float pendingYaw = 0.0F;
    bool historyValid = false;
    bool pendingScaleValid = false;
    bool pendingSuppressed = false;
    bool applied = false;
};

using MouseSdlUpdate = void(__thiscall*)(void*);
// MSVC x86 lowers the eight-byte cVector2l return to a caller-provided pointer
// on the stack. The callee also returns that hidden pointer in EAX.
using MouseSdlGetRelPosition = MouseDelta*(__thiscall*)(void*, MouseDelta*);
using GuiSendMousePos = bool(__thiscall*)(void*, const MouseDelta*, const MouseDelta*);
using LuxUpdateGamePlayerInput = void(__thiscall*)(void*);
using LuxPlayerAddAngle = void(__thiscall*)(void*, float);
using CameraAddAngle = void(__thiscall*)(void*, float);
using SdlGetRelativeMouseState = std::uint32_t(__cdecl*)(int*, int*);
using SdlGetRelativeMouseMode = int(__cdecl*)();
using SdlPumpEvents = void(__cdecl*)();
using SdlGetTicks = std::uint32_t(__cdecl*)();

EngineRun g_originalEngineRun = nullptr;
UpdaterRunMessage g_originalUpdaterRunMessage = nullptr;
SceneRender g_originalSceneRender = nullptr;
PlayerHandsPostUpdate g_originalPlayerHandsPostUpdate = nullptr;
LuxEntityOnUpdate g_originalLuxPropOnUpdate = nullptr;
LuxEntityOnUpdate g_originalLuxEnemyOnUpdate = nullptr;
NewtonTransformCallback g_originalNewtonTransformCallback = nullptr;
NewtonBodyGetUserData g_newtonBodyGetUserData = nullptr;
SubMeshGetModelMatrix g_originalSubMeshGetModelMatrix = nullptr;
ParticleEmitterUpdateGraphics g_originalParticleEmitterUpdateGraphics = nullptr;
ImageTrailRenderEffect g_originalImageTrailRenderEffect = nullptr;
MouseSdlUpdate g_originalMouseSdlUpdate = nullptr;
MouseSdlGetRelPosition g_originalMouseSdlGetRelPosition = nullptr;
GuiSendMousePos g_originalGuiSendMousePos = nullptr;
LuxUpdateGamePlayerInput g_originalLuxUpdateGamePlayerInput = nullptr;
LuxPlayerAddAngle g_originalLuxPlayerAddYaw = nullptr;
LuxPlayerAddAngle g_originalLuxPlayerAddPitch = nullptr;
CameraAddAngle g_originalCameraAddPitch = nullptr;
CameraAddAngle g_originalCameraAddYaw = nullptr;
SdlGetRelativeMouseState g_sdlGetRelativeMouseState = nullptr;
SdlGetRelativeMouseMode g_sdlGetRelativeMouseMode = nullptr;
SdlPumpEvents g_sdlPumpEvents = nullptr;
SdlGetTicks g_sdlGetTicks = nullptr;
void* g_logicTimer = nullptr;
bool g_interpolationClockEnabled = false;
bool g_renderUnlockEnabled = false;
double g_sdlQpcOffsetMilliseconds = 0.0;
bool g_sdlQpcOffsetValid = false;
double g_previousPresentationSeconds = 0.0;
bool g_previousPresentationValid = false;
thread_local bool g_inGamePlayerInput = false;
thread_local std::uint32_t g_playerMouseReadsThisCall = 0;
thread_local std::int32_t g_playerMouseXThisCall = 0;
thread_local std::int32_t g_playerMouseYThisCall = 0;
thread_local void* g_playerCameraThisCall = nullptr;
thread_local float g_playerCameraPitchThisCall = 0.0F;
thread_local float g_playerCameraYawThisCall = 0.0F;
thread_local std::uint32_t g_playerCameraPitchCallsThisCall = 0;
thread_local std::uint32_t g_playerCameraYawCallsThisCall = 0;
thread_local float g_playerMouseSensitivityThisCall = 1.0F;
thread_local bool g_playerMouseInvertThisCall = false;
thread_local bool g_playerContinuousLookThisCall = false;
thread_local bool g_playerYawDequantizedThisCall = false;
thread_local bool g_playerPitchDequantizedThisCall = false;
void* g_mouseDevice = nullptr;
DWORD g_mouseThreadId = 0;
std::atomic<std::int64_t> g_mousePendingX{0};
std::atomic<std::int64_t> g_mousePendingY{0};
HWND g_rawMouseWindow = nullptr;
WNDPROC g_rawMouseOriginalWindowProc = nullptr;
std::atomic<bool> g_rawMouseTapInstalled{false};
std::atomic<bool> g_rawMouseActive{false};
std::atomic<std::int64_t> g_rawMouseSinceRenderX{0};
std::atomic<std::int64_t> g_rawMouseSinceRenderY{0};
std::atomic<std::uint32_t> g_rawMousePackets{0};
std::atomic<std::uint32_t> g_rawMouseNonZeroPackets{0};
std::atomic<std::int64_t> g_rawMousePacketX{0};
std::atomic<std::int64_t> g_rawMousePacketY{0};
std::atomic<std::uint32_t> g_rawMouseTickComparisons{0};
std::atomic<std::uint32_t> g_rawMouseTickMismatches{0};
std::atomic<std::uint32_t> g_rawMouseInstallFailures{0};
std::atomic<std::uint32_t> g_rawMouseDirectDrainCalls{0};
std::atomic<std::uint32_t> g_rawMouseDirectDrainMessages{0};
void* g_guiMouseObject = nullptr;
mouse_preview::GuiPosition g_guiMouseFixedPosition{};
mouse_preview::GuiPosition g_guiMousePresentedPosition{};
std::uint64_t g_guiMouseLastStockTick = 0;
bool g_guiMousePositionValid = false;
std::atomic<std::uint32_t> g_guiMouseStockCalls{0};
std::atomic<std::uint32_t> g_guiMousePresentationCalls{0};
std::atomic<std::uint32_t> g_guiMousePresentationMoves{0};
std::atomic<std::uint32_t> g_guiMouseRelativeSamples{0};
std::atomic<std::uint32_t> g_guiMouseAbsoluteSamples{0};
std::atomic<std::int64_t> g_guiMousePresentationX{0};
std::atomic<std::int64_t> g_guiMousePresentationY{0};
std::atomic<std::uint32_t> g_guiMouseFixedCorrections{0};
std::atomic<std::uint32_t> g_guiMouseStaleSkips{0};
std::atomic<std::uint32_t> g_guiMouseInvalidSkips{0};
std::atomic<std::uint32_t> g_guiMouseThreadMismatches{0};
std::atomic<std::uint64_t> g_mouseLastGameplayTick{0};
std::atomic<bool> g_mousePresentationAcquisitionEligible{false};
bool g_mousePresentationAcquisitionEnabled = false;
bool g_motionTraceEnabled = false;
struct MouseAngularCommand {
    std::uint64_t serial = 0;
    void* camera = nullptr;
    float pitch = 0.0F;
    float yaw = 0.0F;
    bool pendingRawCompatible = false;
    bool valid = false;
};
constexpr std::size_t kMouseCommandHistoryCapacity = 2;
std::array<MouseAngularCommand, kMouseCommandHistoryCapacity> g_mouseCommandHistory{};
float g_mouseSensitivity = 1.0F;
bool g_mouseInvert = false;
float g_mouseYawPerRoundedPixel = 0.0F;
bool g_mouseYawScaleCalibrated = false;
mouse_preview::PendingRawGate g_mousePendingRawGate;
float g_mousePlayerAmountPerRoundedCount = 0.0F;
bool g_mousePlayerAmountScaleCalibrated = false;
void* g_latestPlayerCamera = nullptr;
std::atomic<std::uint32_t> g_mouseUpdates{0};
std::atomic<std::uint32_t> g_mouseAcquiredNonZero{0};
std::atomic<std::uint32_t> g_mouseReads{0};
std::atomic<std::uint32_t> g_mouseRetiredNonZero{0};
std::atomic<std::uint32_t> g_mouseGameplayReads{0};
std::atomic<std::uint32_t> g_mouseUnconsumedOverwrites{0};
std::atomic<std::uint32_t> g_mousePlayerCalls{0};
std::atomic<std::uint32_t> g_mousePlayerEarlyExits{0};
std::atomic<std::uint32_t> g_mousePlayerMultipleReads{0};
std::atomic<std::uint32_t> g_mouseSmoothOn{0};
std::atomic<std::uint32_t> g_mouseSmoothOff{0};
std::atomic<std::uint32_t> g_mouseStateInvalid{0};
std::atomic<std::int64_t> g_mouseAcquiredX{0};
std::atomic<std::int64_t> g_mouseAcquiredY{0};
std::atomic<std::int64_t> g_mouseRetiredX{0};
std::atomic<std::int64_t> g_mouseRetiredY{0};
std::atomic<std::uint32_t> g_mousePresentationPolls{0};
std::atomic<std::uint32_t> g_mousePresentationNonZero{0};
std::atomic<std::int64_t> g_mousePresentationX{0};
std::atomic<std::int64_t> g_mousePresentationY{0};
std::atomic<std::uint32_t> g_mousePendingDeliveries{0};
std::atomic<std::int64_t> g_mousePendingDeliveredX{0};
std::atomic<std::int64_t> g_mousePendingDeliveredY{0};
std::atomic<std::uint32_t> g_mouseTickDeferredPolls{0};
std::atomic<std::uint32_t> g_mouseTickDeferredNonZero{0};
std::atomic<std::int64_t> g_mouseTickDeferredX{0};
std::atomic<std::int64_t> g_mouseTickDeferredY{0};
std::atomic<std::int64_t> g_mouseDeferredSinceRenderX{0};
std::atomic<std::int64_t> g_mouseDeferredSinceRenderY{0};
std::atomic<std::uint32_t> g_mousePresentationThreadMismatches{0};
std::atomic<std::uint32_t> g_mouseMergeOverflows{0};
std::atomic<std::uint32_t> g_mouseCommandCaptured{0};
std::atomic<std::uint32_t> g_mouseCommandMissing{0};
std::atomic<std::uint32_t> g_mouseCommandMultiAdd{0};
std::atomic<std::uint32_t> g_mouseScaleAccepted{0};
std::atomic<std::uint32_t> g_mouseScaleRejected{0};
std::atomic<std::uint32_t> g_mouseScaleFrozen{0};
std::atomic<std::uint32_t> g_mouseCommandDirect{0};
std::atomic<std::uint32_t> g_mouseCommandTransformed{0};
std::atomic<std::uint32_t> g_mouseCommandNoSignal{0};
std::atomic<std::uint32_t> g_mouseCommandShapeInvalid{0};
std::atomic<std::uint32_t> g_mouseFallbackEntered{0};
std::atomic<std::uint32_t> g_mouseFallbackExited{0};
std::atomic<std::uint32_t> g_mousePlayerDequantized{0};
std::atomic<std::uint32_t> g_mousePlayerDequantizeRejected{0};
std::atomic<std::uint32_t> g_mousePreviewApplied{0};
std::atomic<std::uint32_t> g_mousePreviewNoScale{0};
std::atomic<std::uint32_t> g_mousePreviewNoCommand{0};
std::atomic<std::uint32_t> g_mousePreviewPendingSuppressed{0};
std::atomic<std::uint32_t> g_mousePreviewTransformedBypass{0};
std::atomic<std::uint32_t> g_mousePreviewApplyFailed{0};
std::atomic<std::uint32_t> g_mousePreviewYawRebased{0};
std::atomic<std::int64_t> g_mousePreviewPendingX{0};
std::atomic<std::int64_t> g_mousePreviewPendingY{0};
std::atomic<std::uint32_t> g_mouseHandsCorrectionBuilt{0};
std::atomic<std::uint32_t> g_mouseHandsCorrectionApplied{0};
std::atomic<std::uint32_t> g_mouseHandsCorrectionFailed{0};
std::atomic<std::uint32_t> g_completedTicks{0};
std::atomic<std::uint32_t> g_onDrawMessages{0};
std::atomic<std::uint32_t> g_postRenderMessages{0};
std::atomic<std::uint32_t> g_sceneRenders{0};
cadence::FrameCadence g_frameCadence;
std::atomic<std::uint64_t> g_tickSerial{0};
std::atomic<std::uint32_t> g_clockInterpolated{0};
std::atomic<std::uint32_t> g_clockClampedOldest{0};
std::atomic<std::uint32_t> g_clockClampedNewest{0};
std::atomic<std::uint32_t> g_clockUnavailable{0};
std::atomic<std::uint32_t> g_clockResets{0};
std::atomic<std::uint32_t> g_clockPhaseRebases{0};
std::atomic<std::uint32_t> g_latestAlphaMillionths{0};
std::atomic<clock::ResetReason> g_lastClockReset{clock::ResetReason::none};
clock::PresentationClock g_presentationClock;
camera::Runtime g_cameraRuntime;
bool g_cameraInterpolationEnabled = false;
hands::Runtime g_handsRuntime;
bool g_handsInterpolationEnabled = false;
world::Runtime g_worldRuntime;
bool g_worldInterpolationEnabled = false;
pose::Runtime g_poseRuntime;
bool g_handsPoseInterpolationEnabled = false;
bool g_poseInvalidDiagnosticLogged = false;
enemy_pose::Runtime g_enemyPoseRuntime;
bool g_worldPoseInterpolationEnabled = false;
bool g_ragdollPoseInterpolationEnabled = true;
bool g_particleInterpolationEnabled = false;
thread_local bool g_particlePresentationActive = false;
thread_local bool g_particleDetourActive = false;
thread_local double g_particlePresentationAlpha = 0.0;
constexpr std::size_t kParticleScratchCapacity = 4096;
std::array<void*, kParticleScratchCapacity> g_particlePointers{};
std::array<particle::Vec3, kParticleScratchCapacity> g_particleOriginalPositions{};
std::array<particle::Vec3, kParticleScratchCapacity> g_particlePresentedPositions{};
std::atomic<std::uint32_t> g_particleCalls{0};
std::atomic<std::uint32_t> g_particleEmittersApplied{0};
std::atomic<std::uint32_t> g_particlePositionsApplied{0};
std::atomic<std::uint32_t> g_particleInactive{0};
std::atomic<std::uint32_t> g_particleInvalid{0};
std::atomic<std::uint32_t> g_particleCapacity{0};
std::atomic<std::uint32_t> g_particleRestoreFailures{0};
thread_local bool g_imageTrailDetourActive = false;
std::atomic<std::uint32_t> g_imageTrailCalls{0};
std::atomic<std::uint32_t> g_imageTrailAdjusted{0};
std::atomic<std::uint32_t> g_imageTrailUnchanged{0};
std::atomic<std::uint32_t> g_imageTrailClears{0};
std::atomic<std::uint32_t> g_imageTrailInvalid{0};
std::atomic<std::uint32_t> g_imageTrailNested{0};
std::atomic<std::uint32_t> g_imageTrailRestoreFailures{0};
std::atomic<std::uint32_t> g_cameraCaptured{0};
std::atomic<std::uint32_t> g_cameraApplied{0};
std::atomic<std::uint32_t> g_cameraNoScene{0};
std::atomic<std::uint32_t> g_cameraNoView{0};
std::atomic<std::uint32_t> g_cameraNoListener{0};
std::atomic<std::uint32_t> g_cameraListenerHidden{0};
std::atomic<std::uint32_t> g_cameraListenerMissingCamera{0};
std::atomic<std::uint32_t> g_cameraListenerMissingWorld{0};
std::atomic<std::uint32_t> g_cameraInvalidViewport{0};
std::atomic<std::uint32_t> g_cameraIdentityChanges{0};
std::atomic<std::uint32_t> g_cameraGenerationResets{0};
std::atomic<std::uint32_t> g_cameraNonMonotonicResets{0};
std::atomic<std::uint32_t> g_cameraSameTickMutations{0};
std::atomic<std::uint32_t> g_cameraDiscontinuityResets{0};
std::atomic<std::uint32_t> g_cameraClockWaits{0};
std::atomic<std::uint32_t> g_cameraSnapshotMisses{0};
std::atomic<std::uint32_t> g_cameraInvalid{0};
std::atomic<std::uint32_t> g_cameraApplyFailures{0};
std::atomic<std::uint32_t> g_cameraRestoreFailures{0};
std::atomic<camera::FrameResult> g_lastCameraResult{camera::FrameResult::clockUnavailable};
std::atomic<std::uint32_t> g_handsStaged{0};
std::atomic<std::uint32_t> g_handsCaptured{0};
std::atomic<std::uint32_t> g_handsRenderReady{0};
std::atomic<std::uint32_t> g_handsModelOverrides{0};
std::atomic<std::uint32_t> g_handObjectModelOverrides{0};
std::atomic<std::uint32_t> g_handObjectHistoryOverrides{0};
std::atomic<std::uint32_t> g_handsUnavailable{0};
std::atomic<std::uint32_t> g_handsIdentityChanges{0};
std::atomic<std::uint32_t> g_handsResets{0};
std::atomic<std::uint32_t> g_handsClockWaits{0};
std::atomic<std::uint32_t> g_handsSnapshotMisses{0};
std::atomic<std::uint32_t> g_handsInvalid{0};
std::atomic<std::uint32_t> g_handsApplyFailures{0};
std::atomic<std::uint32_t> g_handsSlotExhausted{0};
std::atomic<std::uint32_t> g_handAttachmentCandidates{0};
std::atomic<std::uint32_t> g_handAttachmentApplied{0};
std::atomic<std::uint32_t> g_handAttachmentParentMismatch{0};
std::atomic<std::uint32_t> g_handAttachmentInvalid{0};
std::atomic<std::uint32_t> g_handAttachmentCapacity{0};
std::atomic<std::uint32_t> g_handAttachmentRestoreFailures{0};
std::atomic<hands::FrameResult> g_lastHandsResult{hands::FrameResult::clockUnavailable};
std::atomic<std::uint32_t> g_worldPropsStaged{0};
std::atomic<std::uint32_t> g_worldGruntsStaged{0};
std::atomic<std::uint32_t> g_worldCaptured{0};
std::atomic<std::uint32_t> g_worldSleeping{0};
std::atomic<std::uint32_t> g_worldCachedSleeping{0};
std::atomic<std::uint32_t> g_worldMovedCallbacks{0};
std::atomic<std::uint32_t> g_worldMovedMatched{0};
std::atomic<std::uint32_t> g_worldMovedStandalone{0};
std::atomic<std::uint32_t> g_worldMovedOverflow{0};
std::atomic<std::uint32_t> g_worldReady{0};
std::atomic<std::uint32_t> g_worldPropModels{0};
std::atomic<std::uint32_t> g_worldPropParentModels{0};
std::atomic<std::uint32_t> g_worldPropNullModels{0};
std::atomic<std::uint32_t> g_worldPropDirectParentModels{0};
std::atomic<std::uint32_t> g_worldPropModelHistoryModels{0};
std::atomic<std::uint32_t> g_worldMultiBodyOwners{0};
std::atomic<std::uint32_t> g_worldModelCandidates{0};
std::atomic<std::uint32_t> g_worldModelCaptured{0};
std::atomic<std::uint32_t> g_worldModelInvalid{0};
std::atomic<std::uint32_t> g_worldModelCapacity{0};
std::atomic<std::uint32_t> g_worldModelReady{0};
std::atomic<std::uint32_t> g_worldModelMovingReady{0};
std::atomic<std::uint32_t> g_worldModelCacheApplied{0};
std::atomic<std::uint32_t> g_worldModelCacheInvalid{0};
std::atomic<std::uint32_t> g_worldModelRestoreFailed{0};
std::atomic<std::uint32_t> g_worldModelHistorySeen{0};
std::atomic<std::uint32_t> g_worldModelHistoryBodyPriority{0};
std::atomic<std::uint32_t> g_worldModelHistoryOwnerInvalid{0};
std::atomic<std::uint32_t> g_worldModelHistoryOwnerMissing{0};
std::atomic<std::uint32_t> g_worldModelHistoryOwnerNotReady{0};
std::atomic<std::uint32_t> g_worldModelHistoryStale{0};
std::atomic<std::uint32_t> g_worldModelHistoryClockWait{0};
std::atomic<std::uint32_t> g_worldModelHistoryGeneration{0};
std::atomic<std::uint32_t> g_worldModelHistoryPairMiss{0};
std::atomic<std::uint32_t> g_worldModelHistoryInvalid{0};
std::atomic<std::uint32_t> g_worldModelHistoryMovingUnchanged{0};
std::atomic<std::uint32_t> g_worldModelHistoryMovingChanged{0};
std::atomic<std::uint32_t> g_worldGruntModels{0};
std::atomic<std::uint32_t> g_worldStandaloneModels{0};
std::atomic<std::uint32_t> g_worldStandaloneNullModels{0};
std::atomic<std::uint32_t> g_worldResets{0};
std::atomic<std::uint32_t> g_worldPairMisses{0};
std::atomic<std::uint32_t> g_worldInvalid{0};
std::atomic<std::uint32_t> g_worldApplyFailures{0};
std::atomic<std::uint32_t> g_worldParentMisses{0};
std::atomic<std::uint32_t> g_worldCapacityExhausted{0};
std::atomic<std::uint32_t> g_worldModelSlotsExhausted{0};
std::atomic<std::uint32_t> g_poseStaged{0};
std::atomic<std::uint32_t> g_poseApplied{0};
std::atomic<std::uint32_t> g_poseAlreadyApplied{0};
std::atomic<std::uint32_t> g_poseUnavailable{0};
std::atomic<std::uint32_t> g_poseIdentityChanges{0};
std::atomic<std::uint32_t> g_poseClockWaits{0};
std::atomic<std::uint32_t> g_posePairMisses{0};
std::atomic<std::uint32_t> g_poseInvalid{0};
std::atomic<std::uint32_t> g_poseRestoreFailures{0};
std::atomic<std::uint32_t> g_enemyPoseEnemyStaged{0};
std::atomic<std::uint32_t> g_enemyPoseRagdollStaged{0};
std::atomic<std::uint32_t> g_enemyPoseTargets{0};
std::atomic<std::uint32_t> g_enemyPoseVisibilitySkipped{0};
std::atomic<std::uint32_t> g_enemyPoseVisibilityUnavailable{0};
std::atomic<std::uint32_t> g_enemyPoseCaptured{0};
std::atomic<std::uint32_t> g_enemyPoseApplied{0};
std::atomic<std::uint32_t> g_enemyPoseIdentityChanges{0};
std::atomic<std::uint32_t> g_enemyPoseGenerationResets{0};
std::atomic<std::uint32_t> g_enemyPoseNonMonotonicResets{0};
std::atomic<std::uint32_t> g_enemyPoseSameTickMutations{0};
std::atomic<std::uint32_t> g_enemyPoseBoneCountResets{0};
std::atomic<std::uint32_t> g_enemyPoseDiscontinuityResets{0};
std::atomic<std::uint32_t> g_enemyPoseClockWaits{0};
std::atomic<std::uint32_t> g_enemyPosePairMisses{0};
std::atomic<std::uint32_t> g_enemyPoseInvalid{0};
std::atomic<std::uint32_t> g_enemyPoseUnavailable{0};
std::atomic<std::uint32_t> g_enemyPoseRestoreFailures{0};
std::atomic<std::uint32_t> g_enemyPoseCapacityExhausted{0};
LARGE_INTEGER g_qpcFrequency{};
HANDLE g_stopEvent = nullptr;
unsigned int g_reportIntervalSeconds = 5;

enum class RenderUnlockState : std::uint32_t {
    disabled,
    pending,
    active,
    rejected,
    finished,
};

std::atomic<RenderUnlockState> g_renderUnlockState{RenderUnlockState::disabled};
constexpr std::size_t kEngineLimitFpsOffset = 0x28;
constexpr std::size_t kEngineLogicTimerOffset = 0x18;
constexpr std::size_t kLogicTimerLocalTimeOffset = 0x00;
constexpr std::size_t kLogicTimerStepMillisecondsOffset = 0x08;
constexpr std::size_t kLogicTimerSpeedMultiplierOffset = 0x10;
constexpr std::size_t kLogicTimerReadableBytes = 0x20;
// Center the presentation target within the scheduling window. A full render
// interval can fall just before the retained previous state when HPL completes
// a tick between two renders; 55% preserves a small post-deadline guard while
// substantially reducing endpoint clamps at 240 Hz.
constexpr double kPresentationHeadroomFraction = 0.55;

double QpcSeconds() noexcept {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(g_qpcFrequency.QuadPart);
}

struct ProcessWindowSearch {
    DWORD processId = 0;
    HWND window = nullptr;
};

BOOL CALLBACK FindProcessWindow(const HWND window, const LPARAM parameter) noexcept {
    auto* const search = reinterpret_cast<ProcessWindowSearch*>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != search->processId || !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }
    search->window = window;
    return FALSE;
}

LRESULT CALLBACK RawMouseWindowProc(const HWND window, const UINT message,
                                    const WPARAM wParam, const LPARAM lParam) noexcept {
    if (message == WM_INPUT) {
        RAWINPUT input{};
        UINT bytes = sizeof(input);
        const UINT copied = GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
                                            &input, &bytes, sizeof(RAWINPUTHEADER));
        if (copied != static_cast<UINT>(-1) && copied >= sizeof(RAWINPUTHEADER) &&
            input.header.dwType == RIM_TYPEMOUSE &&
            (input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
            const std::int32_t x = input.data.mouse.lLastX;
            const std::int32_t y = input.data.mouse.lLastY;
            g_rawMouseActive.store(true, std::memory_order_release);
            g_rawMousePackets.fetch_add(1, std::memory_order_relaxed);
            if (x != 0 || y != 0) {
                g_rawMouseNonZeroPackets.fetch_add(1, std::memory_order_relaxed);
            }
            g_rawMousePacketX.fetch_add(x, std::memory_order_relaxed);
            g_rawMousePacketY.fetch_add(y, std::memory_order_relaxed);
            g_rawMouseSinceRenderX.fetch_add(x, std::memory_order_relaxed);
            g_rawMouseSinceRenderY.fetch_add(y, std::memory_order_relaxed);
            g_mousePendingX.fetch_add(x, std::memory_order_relaxed);
            g_mousePendingY.fetch_add(y, std::memory_order_relaxed);
        }
    }

    const WNDPROC original = g_rawMouseOriginalWindowProc;
    if (original == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return CallWindowProcW(original, window, message, wParam, lParam);
}

bool EnsureRawMouseTapInstalled() noexcept {
    if (g_rawMouseTapInstalled.load(std::memory_order_acquire)) {
        return true;
    }

    const DWORD processId = GetCurrentProcessId();
    HWND window = GetForegroundWindow();
    DWORD windowProcessId = 0;
    if (window != nullptr) {
        GetWindowThreadProcessId(window, &windowProcessId);
    }
    if (window == nullptr || windowProcessId != processId) {
        ProcessWindowSearch search{processId, nullptr};
        EnumWindows(&FindProcessWindow, reinterpret_cast<LPARAM>(&search));
        window = search.window;
    }
    if (window == nullptr) {
        g_rawMouseInstallFailures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(
        window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&RawMouseWindowProc));
    if (previous == 0 && GetLastError() != ERROR_SUCCESS) {
        g_rawMouseInstallFailures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    g_rawMouseOriginalWindowProc = reinterpret_cast<WNDPROC>(previous);
    g_rawMouseWindow = window;
    g_rawMouseTapInstalled.store(true, std::memory_order_release);
    log::Write(log::Level::info,
               L"Native raw-mouse packet tap installed on the SDL game window; "
               L"SDL remains responsible for registration, buttons, focus, and menus.");
    return true;
}

void DrainRawMouseMessages() noexcept {
    const HWND window = g_rawMouseWindow;
    if (window == nullptr || !g_rawMouseTapInstalled.load(std::memory_order_acquire)) {
        return;
    }

    g_rawMouseDirectDrainCalls.fetch_add(1, std::memory_order_relaxed);
    std::uint32_t dispatched = 0;
    MSG message{};
    while (dispatched < 8192U &&
           PeekMessageW(&message, window, WM_INPUT, WM_INPUT, PM_REMOVE)) {
        DispatchMessageW(&message);
        ++dispatched;
    }
    g_rawMouseDirectDrainMessages.fetch_add(dispatched, std::memory_order_relaxed);
}

bool IsReadableMemory(const void* address, const std::size_t bytes) noexcept {
    if (address == nullptr || bytes == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
        return false;
    }
    constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                               PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                               PAGE_EXECUTE_WRITECOPY;
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto regionBegin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto regionEnd = regionBegin + memory.RegionSize;
    return (memory.Protect & readable) != 0 && begin >= regionBegin &&
           begin <= regionEnd && bytes <= regionEnd - begin;
}

bool __fastcall ParticleEmitterUpdateGraphicsDetour(void* self, void*, void* frustum,
                                                     const float frameTime) {
    g_particleCalls.fetch_add(1, std::memory_order_relaxed);
    if (!g_particlePresentationActive || g_particleDetourActive || self == nullptr) {
        g_particleInactive.fetch_add(1, std::memory_order_relaxed);
        return g_originalParticleEmitterUpdateGraphics(self, frustum, frameTime);
    }

    constexpr std::size_t kParticlesBeginOffset = 0x39C;
    constexpr std::size_t kParticlesEndOffset = 0x3A0;
    constexpr std::size_t kParticleCountOffset = 0x3AC;
    constexpr std::size_t kParticleMaximumOffset = 0x3B0;
    constexpr std::size_t kLastPositionOffset = sizeof(particle::Vec3);

    const auto* emitter = static_cast<const std::uint8_t*>(self);
    auto* const particles = *reinterpret_cast<void* const*>(
        emitter + kParticlesBeginOffset);
    auto* const particlesEnd = *reinterpret_cast<void* const*>(
        emitter + kParticlesEndOffset);
    const std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(
        emitter + kParticleCountOffset);
    const std::uint32_t maximum = *reinterpret_cast<const std::uint32_t*>(
        emitter + kParticleMaximumOffset);
    if (count == 0) {
        return g_originalParticleEmitterUpdateGraphics(self, frustum, frameTime);
    }

    const auto beginAddress = reinterpret_cast<std::uintptr_t>(particles);
    const auto endAddress = reinterpret_cast<std::uintptr_t>(particlesEnd);
    if (particles == nullptr || endAddress < beginAddress ||
        (endAddress - beginAddress) % sizeof(void*) != 0 ||
        count > maximum || count > (endAddress - beginAddress) / sizeof(void*)) {
        g_particleInvalid.fetch_add(1, std::memory_order_relaxed);
        return g_originalParticleEmitterUpdateGraphics(self, frustum, frameTime);
    }
    if (count > kParticleScratchCapacity) {
        g_particleCapacity.fetch_add(1, std::memory_order_relaxed);
        return g_originalParticleEmitterUpdateGraphics(self, frustum, frameTime);
    }

    const auto* const particleArray = static_cast<void* const*>(particles);
    for (std::size_t index = 0; index < count; ++index) {
        auto* const particleObject = static_cast<std::uint8_t*>(particleArray[index]);
        if (particleObject == nullptr) {
            g_particleInvalid.fetch_add(1, std::memory_order_relaxed);
            return g_originalParticleEmitterUpdateGraphics(self, frustum, frameTime);
        }

        const auto current = *reinterpret_cast<const particle::Vec3*>(particleObject);
        const auto previous = *reinterpret_cast<const particle::Vec3*>(
            particleObject + kLastPositionOffset);
        particle::Vec3 presented{};
        if (!particle::Interpolate(previous, current, g_particlePresentationAlpha,
                                   presented)) {
            g_particleInvalid.fetch_add(1, std::memory_order_relaxed);
            return g_originalParticleEmitterUpdateGraphics(self, frustum, frameTime);
        }
        g_particlePointers[index] = particleObject;
        g_particleOriginalPositions[index] = current;
        g_particlePresentedPositions[index] = presented;
    }

    g_particleDetourActive = true;
    for (std::size_t index = 0; index < count; ++index) {
        *static_cast<particle::Vec3*>(g_particlePointers[index]) =
            g_particlePresentedPositions[index];
    }
    const bool result = g_originalParticleEmitterUpdateGraphics(self, frustum, frameTime);
    for (std::size_t index = 0; index < count; ++index) {
        auto* const position = static_cast<particle::Vec3*>(g_particlePointers[index]);
        *position = g_particleOriginalPositions[index];
    }
    g_particleDetourActive = false;
    g_particleEmittersApplied.fetch_add(1, std::memory_order_relaxed);
    g_particlePositionsApplied.fetch_add(count, std::memory_order_relaxed);
    return result;
}

void* __fastcall ImageTrailRenderEffectDetour(void* self, void*, void* inputTexture,
                                               void* finalTempBuffer) {
    g_imageTrailCalls.fetch_add(1, std::memory_order_relaxed);
    if (self == nullptr) {
        g_imageTrailInvalid.fetch_add(1, std::memory_order_relaxed);
        return g_originalImageTrailRenderEffect(self, inputTexture, finalTempBuffer);
    }
    if (g_imageTrailDetourActive) {
        g_imageTrailNested.fetch_add(1, std::memory_order_relaxed);
        return g_originalImageTrailRenderEffect(self, inputTexture, finalTempBuffer);
    }

    constexpr std::size_t kCompositeOffset = 0x18;
    constexpr std::size_t kAmountOffset = 0x4C;
    constexpr std::size_t kClearFrameBufferOffset = 0x50;
    constexpr std::size_t kCompositeFrameTimeOffset = 0x11C;
    auto* const effect = static_cast<std::uint8_t*>(self);
    if (*(effect + kClearFrameBufferOffset) != 0) {
        g_imageTrailClears.fetch_add(1, std::memory_order_relaxed);
        return g_originalImageTrailRenderEffect(self, inputTexture, finalTempBuffer);
    }

    auto* const composite = *reinterpret_cast<std::uint8_t* const*>(
        effect + kCompositeOffset);
    if (composite == nullptr) {
        g_imageTrailInvalid.fetch_add(1, std::memory_order_relaxed);
        return g_originalImageTrailRenderEffect(self, inputTexture, finalTempBuffer);
    }

    auto* const amountAddress = reinterpret_cast<float*>(effect + kAmountOffset);
    const float originalAmount = *amountAddress;
    const float frameTime = *reinterpret_cast<const float*>(
        composite + kCompositeFrameTimeOffset);
    float correctedAmount = 0.0F;
    if (!image_trail::CorrectAmount(originalAmount, frameTime, correctedAmount)) {
        g_imageTrailInvalid.fetch_add(1, std::memory_order_relaxed);
        return g_originalImageTrailRenderEffect(self, inputTexture, finalTempBuffer);
    }

    if (std::memcmp(&correctedAmount, &originalAmount, sizeof(float)) == 0) {
        g_imageTrailUnchanged.fetch_add(1, std::memory_order_relaxed);
        return g_originalImageTrailRenderEffect(self, inputTexture, finalTempBuffer);
    }

    g_imageTrailDetourActive = true;
    *amountAddress = correctedAmount;
    void* const result = g_originalImageTrailRenderEffect(
        self, inputTexture, finalTempBuffer);
    *amountAddress = originalAmount;
    g_imageTrailDetourActive = false;
    if (std::memcmp(amountAddress, &originalAmount, sizeof(float)) != 0) {
        g_imageTrailRestoreFailures.fetch_add(1, std::memory_order_relaxed);
    }
    g_imageTrailAdjusted.fetch_add(1, std::memory_order_relaxed);
    return result;
}

bool __fastcall GuiSendMousePosDetour(void* self, void*, const MouseDelta* absolute,
                                      const MouseDelta* relative) {
    if (self == nullptr || absolute == nullptr || relative == nullptr ||
        !IsReadableMemory(static_cast<const std::uint8_t*>(self) + 0x34,
                          sizeof(void*))) {
        g_guiMousePositionValid = false;
        g_guiMouseInvalidSkips.fetch_add(1, std::memory_order_relaxed);
        return g_originalGuiSendMousePos(self, absolute, relative);
    }

    const void* const focusedSet = *reinterpret_cast<void* const*>(
        static_cast<const std::uint8_t*>(self) + 0x34);
    if (focusedSet == nullptr) {
        g_guiMousePositionValid = false;
        return g_originalGuiSendMousePos(self, absolute, relative);
    }

    MouseDelta unpresented = *relative;
    if (g_guiMousePositionValid && g_guiMouseObject == self) {
        const auto delta = mouse_preview::GuiRelativeDelta(
            g_guiMousePresentedPosition, {absolute->x, absolute->y});
        unpresented = {delta.x, delta.y};
        if (unpresented.x != relative->x || unpresented.y != relative->y) {
            g_guiMouseFixedCorrections.fetch_add(1, std::memory_order_relaxed);
        }
    }

    const bool result = g_originalGuiSendMousePos(self, absolute, &unpresented);
    g_guiMouseObject = self;
    g_guiMouseFixedPosition = {absolute->x, absolute->y};
    g_guiMousePresentedPosition = g_guiMouseFixedPosition;
    g_guiMouseLastStockTick = g_tickSerial.load(std::memory_order_relaxed);
    g_guiMousePositionValid = true;
    g_mousePresentationAcquisitionEligible.store(false, std::memory_order_relaxed);
    g_guiMouseStockCalls.fetch_add(1, std::memory_order_relaxed);
    return result;
}

void ApplyGuiMousePresentation() noexcept {
    if (!g_mousePresentationAcquisitionEnabled || !g_guiMousePositionValid ||
        g_guiMouseObject == nullptr || g_originalGuiSendMousePos == nullptr ||
        g_sdlGetRelativeMouseMode == nullptr || g_rawMouseWindow == nullptr ||
        !g_rawMouseTapInstalled.load(std::memory_order_acquire) ||
        g_mousePresentationAcquisitionEligible.load(std::memory_order_relaxed)) {
        return;
    }
    const std::uint64_t tick = g_tickSerial.load(std::memory_order_relaxed);
    if (tick > g_guiMouseLastStockTick + 1) {
        g_guiMousePositionValid = false;
        g_guiMouseStaleSkips.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (g_mouseThreadId == 0 || GetCurrentThreadId() != g_mouseThreadId) {
        g_guiMouseThreadMismatches.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    RECT client{};
    if (!GetClientRect(g_rawMouseWindow, &client) || client.right <= client.left ||
        client.bottom <= client.top) {
        g_guiMouseInvalidSkips.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    mouse_preview::GuiPosition position{};
    if (g_sdlGetRelativeMouseMode() != 0) {
        // Captured inventory and gameplay GUI states use relative input. Drain
        // only native packets; SDL retains its event queue and buttons.
        DrainRawMouseMessages();
        if (!g_rawMouseActive.load(std::memory_order_acquire)) {
            return;
        }
        position = mouse_preview::PreviewGuiPosition(
            g_guiMouseFixedPosition,
            g_mousePendingX.load(std::memory_order_relaxed),
            g_mousePendingY.load(std::memory_order_relaxed),
            client.right - client.left,
            client.bottom - client.top);
        g_guiMouseRelativeSamples.fetch_add(1, std::memory_order_relaxed);
    } else {
        // Main and pause menus explicitly disable SDL relative mode. Raw input
        // packets stop in this state, while the OS cursor continues moving at
        // device/presentation cadence. Use its client position directly so
        // native desktop acceleration and multi-monitor coordinates survive.
        POINT cursor{};
        if (!GetCursorPos(&cursor) || !ScreenToClient(g_rawMouseWindow, &cursor)) {
            g_guiMouseInvalidSkips.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        position = mouse_preview::PreviewGuiPosition(
            {cursor.x, cursor.y}, 0, 0,
            client.right - client.left,
            client.bottom - client.top);
        g_guiMouseAbsoluteSamples.fetch_add(1, std::memory_order_relaxed);
    }
    g_guiMousePresentationCalls.fetch_add(1, std::memory_order_relaxed);
    const auto relative = mouse_preview::GuiRelativeDelta(
        g_guiMousePresentedPosition, position);
    if (relative.x == 0 && relative.y == 0) {
        return;
    }

    const MouseDelta absolute{position.x, position.y};
    const MouseDelta movement{relative.x, relative.y};
    (void)g_originalGuiSendMousePos(g_guiMouseObject, &absolute, &movement);
    g_guiMousePresentedPosition = position;
    g_guiMousePresentationMoves.fetch_add(1, std::memory_order_relaxed);
    g_guiMousePresentationX.fetch_add(relative.x, std::memory_order_relaxed);
    g_guiMousePresentationY.fetch_add(relative.y, std::memory_order_relaxed);
}

bool ReadLogicTimerAlpha(const double qpcSeconds,
                         const double presentationDelayTicks,
                         double& alpha,
                         double& rawAlpha) noexcept {
    alpha = 0.0;
    rawAlpha = 0.0;
    if (!g_interpolationClockEnabled || g_logicTimer == nullptr ||
        g_sdlGetTicks == nullptr || !g_sdlQpcOffsetValid ||
        !IsReadableMemory(g_logicTimer, kLogicTimerReadableBytes)) {
        return false;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(g_logicTimer);
    const double localTime = *reinterpret_cast<const double*>(
        bytes + kLogicTimerLocalTimeOffset);
    const double stepMilliseconds = *reinterpret_cast<const double*>(
        bytes + kLogicTimerStepMillisecondsOffset);
    const double speedMultiplier = *reinterpret_cast<const double*>(
        bytes + kLogicTimerSpeedMultiplierOffset);
    const double nowMilliseconds =
        qpcSeconds * 1000.0 + g_sdlQpcOffsetMilliseconds;
    return clock::LogicTimerAlpha(localTime, stepMilliseconds,
                                  speedMultiplier, nowMilliseconds,
                                  presentationDelayTicks, alpha, &rawAlpha);
}

std::int32_t SaturatingAddMouse(const std::int32_t value,
                                const std::int64_t pending) noexcept {
    const std::int64_t sum = static_cast<std::int64_t>(value) + pending;
    if (sum > std::numeric_limits<std::int32_t>::max()) {
        g_mouseMergeOverflows.fetch_add(1, std::memory_order_relaxed);
        return std::numeric_limits<std::int32_t>::max();
    }
    if (sum < std::numeric_limits<std::int32_t>::min()) {
        g_mouseMergeOverflows.fetch_add(1, std::memory_order_relaxed);
        return std::numeric_limits<std::int32_t>::min();
    }
    return static_cast<std::int32_t>(sum);
}

PresentationMousePoll AcquirePresentationMouse() noexcept {
    PresentationMousePoll poll{};
    if (!g_mousePresentationAcquisitionEnabled ||
        !g_mousePresentationAcquisitionEligible.load(std::memory_order_relaxed) ||
        g_mouseDevice == nullptr || g_sdlGetRelativeMouseState == nullptr ||
        g_sdlPumpEvents == nullptr) {
        return poll;
    }
    const std::uint64_t tick = g_tickSerial.load(std::memory_order_relaxed);
    const std::uint64_t lastGameplayTick =
        g_mouseLastGameplayTick.load(std::memory_order_relaxed);
    if (lastGameplayTick == 0 || tick > lastGameplayTick + 1) {
        return poll;
    }
    if (g_mouseThreadId == 0 || GetCurrentThreadId() != g_mouseThreadId) {
        g_mousePresentationThreadMismatches.fetch_add(1, std::memory_order_relaxed);
        return poll;
    }

    int x = 0;
    int y = 0;
    // HPL's fixed update repeatedly calls SDL_PollEvent until its queue is
    // empty. A lone SDL_PumpEvents here did not drain relative packets with
    // equivalent cadence, leaving a large batch for the 60 Hz loop. Remove and
    // dispatch only WM_INPUT from the SDL window before and after its normal
    // pump; all other message ordering and SDL ownership remain untouched.
    DrainRawMouseMessages();
    g_sdlPumpEvents();
    DrainRawMouseMessages();
    if (g_rawMouseActive.load(std::memory_order_acquire)) {
        x = SaturatingAddMouse(
            0, g_rawMouseSinceRenderX.exchange(0, std::memory_order_relaxed));
        y = SaturatingAddMouse(
            0, g_rawMouseSinceRenderY.exchange(0, std::memory_order_relaxed));
    } else {
        // Fail closed to the SDL accumulator until its Win32 backend proves that
        // relative WM_INPUT packets are reaching the game window.
        (void)g_sdlGetRelativeMouseState(&x, &y);
        g_mousePendingX.fetch_add(x, std::memory_order_relaxed);
        g_mousePendingY.fetch_add(y, std::memory_order_relaxed);
    }
    poll.x = x;
    poll.y = y;
    poll.polled = true;
    g_mousePresentationPolls.fetch_add(1, std::memory_order_relaxed);
    if (x != 0 || y != 0) {
        g_mousePresentationNonZero.fetch_add(1, std::memory_order_relaxed);
    }
    g_mousePresentationX.fetch_add(x, std::memory_order_relaxed);
    g_mousePresentationY.fetch_add(y, std::memory_order_relaxed);
    return poll;
}

std::int64_t RoundedMousePixels(const std::int64_t raw, const float sensitivity) noexcept {
    const double scaled = static_cast<double>(raw) * static_cast<double>(sensitivity);
    if (!std::isfinite(scaled)) {
        return 0;
    }
    const double minimum = static_cast<double>(std::numeric_limits<std::int64_t>::min());
    const double maximum = static_cast<double>(std::numeric_limits<std::int64_t>::max());
    if (scaled <= minimum) {
        return std::numeric_limits<std::int64_t>::min();
    }
    if (scaled >= maximum) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(std::llround(scaled));
}

bool DequantizePlayerLookAmount(const float stockAmount,
                                const std::int32_t rawCounts,
                                const bool pitch,
                                float& adjustedAmount) noexcept {
    adjustedAmount = stockAmount;
    if (!g_inGamePlayerInput || !g_playerContinuousLookThisCall ||
        g_playerMouseReadsThisCall != 1U || rawCounts == 0 ||
        !std::isfinite(stockAmount)) {
        return false;
    }
    const std::int64_t rounded = RoundedMousePixels(
        rawCounts, g_playerMouseSensitivityThisCall);
    if (rounded == 0) {
        return false;
    }

    const float candidate = stockAmount / static_cast<float>(rounded);
    const float candidateMagnitude =
        pitch && g_playerMouseInvertThisCall ? candidate : -candidate;
    if (!std::isfinite(candidateMagnitude) || candidateMagnitude <= 0.0000001F ||
        candidateMagnitude >= 0.1F) {
        g_mousePlayerDequantizeRejected.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (!g_mousePlayerAmountScaleCalibrated) {
        g_mousePlayerAmountPerRoundedCount = candidateMagnitude;
        g_mousePlayerAmountScaleCalibrated = true;
    } else {
        const float tolerance = std::fmax(
            g_mousePlayerAmountPerRoundedCount * 0.02F, 0.0000001F);
        if (std::fabs(candidateMagnitude - g_mousePlayerAmountPerRoundedCount) >
            tolerance) {
            g_mousePlayerDequantizeRejected.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    const float signedScale = pitch && g_playerMouseInvertThisCall
                                  ? g_mousePlayerAmountPerRoundedCount
                                  : -g_mousePlayerAmountPerRoundedCount;
    if (!mouse_preview::ContinuousPlayerAmount(
            rawCounts, g_playerMouseSensitivityThisCall, rounded, stockAmount,
            signedScale, adjustedAmount)) {
        g_mousePlayerDequantizeRejected.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    g_mousePlayerDequantized.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void CaptureCameraAngularDelta(void* camera, const float delta,
                               const bool pitch) noexcept {
    if (!g_inGamePlayerInput || camera == nullptr || !std::isfinite(delta)) {
        return;
    }
    if (g_playerCameraThisCall == nullptr) {
        g_playerCameraThisCall = camera;
    } else if (g_playerCameraThisCall != camera) {
        g_mouseStateInvalid.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (pitch) {
        g_playerCameraPitchThisCall += delta;
        ++g_playerCameraPitchCallsThisCall;
    } else {
        g_playerCameraYawThisCall += delta;
        ++g_playerCameraYawCallsThisCall;
    }
}

void PublishMouseAngularCommand(const float sensitivity, const bool invert) noexcept {
    const std::uint64_t serial = g_tickSerial.load(std::memory_order_relaxed) + 1;
    MouseAngularCommand command{};
    command.serial = serial;
    command.camera = g_playerCameraThisCall;
    command.pitch = g_playerCameraPitchThisCall;
    command.yaw = g_playerCameraYawThisCall;
    command.valid = g_playerMouseReadsThisCall == 1U && command.camera != nullptr &&
                    std::isfinite(command.pitch) && std::isfinite(command.yaw);
    if (g_playerCameraPitchCallsThisCall > 1U ||
        g_playerCameraYawCallsThisCall > 1U) {
        g_mouseCommandMultiAdd.fetch_add(1, std::memory_order_relaxed);
    }
    if (!command.valid) {
        command.pendingRawCompatible = false;
        g_mouseCommandHistory[serial % kMouseCommandHistoryCapacity] = command;
        g_mouseCommandMissing.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_latestPlayerCamera = command.camera;
    g_mouseCommandCaptured.fetch_add(1, std::memory_order_relaxed);

    // The calibrated value is radians per sensitivity-adjusted mouse count;
    // changing sensitivity or pitch inversion does not change that yaw scale.
    // Retaining it is also essential while a camera effect owns the final
    // command, because filtered output is not a valid calibration source.
    g_mouseSensitivity = sensitivity;
    g_mouseInvert = invert;

    const std::int64_t roundedX = RoundedMousePixels(g_playerMouseXThisCall, sensitivity);
    const std::int64_t roundedY = RoundedMousePixels(g_playerMouseYThisCall, sensitivity);
    const double calibrationX = g_playerYawDequantizedThisCall
                                    ? static_cast<double>(g_playerMouseXThisCall) *
                                          static_cast<double>(sensitivity)
                                    : static_cast<double>(roundedX);
    bool candidatePresent = false;
    bool candidateValid = false;
    bool calibratedThisCommand = false;
    float candidate = 0.0F;
    if (std::fabs(calibrationX) >= 0.000001 &&
        std::fabs(command.yaw) >= 0.000001F) {
        candidatePresent = true;
        candidate = command.yaw / static_cast<float>(calibrationX);
        candidateValid = std::isfinite(candidate) && candidate < -0.000001F &&
                         candidate >= -0.1F;
        if (!candidateValid) {
            g_mouseScaleRejected.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (!g_mouseYawScaleCalibrated && candidateValid) {
        g_mouseYawPerRoundedPixel = candidate;
        g_mouseYawScaleCalibrated = true;
        calibratedThisCommand = true;
        g_mouseScaleAccepted.fetch_add(1, std::memory_order_relaxed);
    }

    if (g_mouseYawScaleCalibrated) {
        const double expectedInputX =
            g_playerYawDequantizedThisCall
                ? static_cast<double>(g_playerMouseXThisCall) *
                      static_cast<double>(sensitivity)
                : static_cast<double>(roundedX);
        const double expectedInputY =
            g_playerPitchDequantizedThisCall
                ? static_cast<double>(g_playerMouseYThisCall) *
                      static_cast<double>(sensitivity)
                : static_cast<double>(roundedY);
        const float pitchScale =
            invert ? -g_mouseYawPerRoundedPixel : g_mouseYawPerRoundedPixel;
        const float expectedYaw =
            static_cast<float>(expectedInputX * g_mouseYawPerRoundedPixel);
        const float expectedPitch =
            static_cast<float>(expectedInputY * pitchScale);
        const mouse_preview::CommandShape shape = mouse_preview::ClassifyCommandShape(
            command.pitch, command.yaw, expectedPitch, expectedYaw);
        const bool wasAllowed = g_mousePendingRawGate.Allowed();
        const bool allowed = g_mousePendingRawGate.Update(shape);
        if (wasAllowed && !allowed) {
            g_mouseFallbackEntered.fetch_add(1, std::memory_order_relaxed);
        } else if (!wasAllowed && allowed) {
            g_mouseFallbackExited.fetch_add(1, std::memory_order_relaxed);
        }
        switch (shape) {
        case mouse_preview::CommandShape::direct:
            g_mouseCommandDirect.fetch_add(1, std::memory_order_relaxed);
            break;
        case mouse_preview::CommandShape::transformed:
            g_mouseCommandTransformed.fetch_add(1, std::memory_order_relaxed);
            break;
        case mouse_preview::CommandShape::noSignal:
            g_mouseCommandNoSignal.fetch_add(1, std::memory_order_relaxed);
            break;
        case mouse_preview::CommandShape::invalid:
            g_mouseCommandShapeInvalid.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        // Classify against the pre-existing direct scale before considering
        // this sample for calibration. Once transformed mode is detected, the
        // scale remains frozen until the preserved value recognizes a stable
        // run of direct commands. Otherwise the sanity moving average can drag
        // the scale along with itself and permanently prevent recovery.
        if (candidateValid && mouse_preview::ShouldUpdateDirectScale(
                                  calibratedThisCommand, wasAllowed, allowed, shape)) {
            g_mouseYawPerRoundedPixel =
                g_mouseYawPerRoundedPixel * 0.875F + candidate * 0.125F;
            g_mouseScaleAccepted.fetch_add(1, std::memory_order_relaxed);
        } else if (!calibratedThisCommand && candidatePresent && candidateValid) {
            g_mouseScaleFrozen.fetch_add(1, std::memory_order_relaxed);
        }
        command.pendingRawCompatible = allowed;
    }
    g_mouseCommandHistory[serial % kMouseCommandHistoryCapacity] = command;
}

hands::Matrix CameraWorldMatrix(const camera::Pose& pose) noexcept {
    const float cp = std::cos(pose.pitch);
    const float sp = std::sin(pose.pitch);
    const float cy = std::cos(pose.yaw);
    const float sy = std::sin(pose.yaw);
    const float cr = std::cos(pose.roll);
    const float sr = std::sin(pose.roll);
    hands::Matrix result = hands::Identity();
    result.values[0] = cy * cr + sy * sp * sr;
    result.values[1] = -cy * sr + sy * sp * cr;
    result.values[2] = sy * cp;
    result.values[4] = cp * sr;
    result.values[5] = cp * cr;
    result.values[6] = -sp;
    result.values[8] = -sy * cr + cy * sp * sr;
    result.values[9] = sy * sr + cy * sp * cr;
    result.values[10] = cy * cp;
    result.values[3] = pose.position.x;
    result.values[7] = pose.position.y;
    result.values[11] = pose.position.z;
    return result;
}

bool BuildCameraPresentationCorrection(const camera::Pose& before,
                                       const camera::Pose& after,
                                       hands::Matrix& correction) noexcept {
    const hands::Matrix oldWorld = CameraWorldMatrix(before);
    const hands::Matrix newWorld = CameraWorldMatrix(after);
    hands::Matrix inverseOld{};
    return hands::InverseRigid(oldWorld, inverseOld) &&
           hands::Multiply(newWorld, inverseOld, correction) &&
           hands::IsRigid(correction);
}

bool ApplyMouseAngularPreview(const clock::Sample& sample,
                              const std::uint64_t currentSerial,
                              hands::Matrix& handsCorrection,
                              MousePreviewDiagnostics& diagnostics) noexcept {
    handsCorrection = hands::Identity();
    diagnostics = {};
    diagnostics.pendingX = g_mousePendingX.load(std::memory_order_relaxed);
    diagnostics.pendingY = g_mousePendingY.load(std::memory_order_relaxed);
    if (!g_mousePresentationAcquisitionEnabled ||
        !g_mousePresentationAcquisitionEligible.load(std::memory_order_relaxed)) {
        return false;
    }
    void* const camera = g_cameraRuntime.ActiveCamera();
    if (camera == nullptr || camera != g_latestPlayerCamera || sample.newerSerial == 0 ||
        currentSerial < sample.newerSerial ||
        currentSerial - sample.newerSerial >= kMouseCommandHistoryCapacity) {
        g_mousePreviewNoCommand.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    float pitch = 0.0F;
    float yaw = 0.0F;
    bool pendingRawCompatible = false;
    for (std::uint64_t serial = sample.newerSerial; serial <= currentSerial; ++serial) {
        const MouseAngularCommand& command =
            g_mouseCommandHistory[serial % kMouseCommandHistoryCapacity];
        if (!command.valid || command.serial != serial || command.camera != camera) {
            g_mousePreviewNoCommand.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const float weight = serial == sample.newerSerial
                                 ? static_cast<float>(1.0 - sample.alpha)
                                 : 1.0F;
        pitch += command.pitch * weight;
        yaw += command.yaw * weight;
        pendingRawCompatible = command.pendingRawCompatible;
    }
    diagnostics.historyPitch = pitch;
    diagnostics.historyYaw = yaw;
    diagnostics.historyValid = true;

    const std::int64_t pendingX = diagnostics.pendingX;
    const std::int64_t pendingY = diagnostics.pendingY;
    if (!pendingRawCompatible) {
        g_mousePreviewTransformedBypass.fetch_add(1, std::memory_order_relaxed);
        if (pendingX != 0 || pendingY != 0) {
            diagnostics.pendingSuppressed = true;
            g_mousePreviewPendingSuppressed.fetch_add(1, std::memory_order_relaxed);
        }
        // The base camera runtime is already interpolating HPL's previous and
        // current fixed states. Applying the angular history correction here
        // would advance that interpolation to the newest state and hold it
        // until the next 60 Hz update. A transformed command therefore owns
        // the camera entirely until the direct raw relationship is stable.
        return false;
    }
    if (pendingX != 0 || pendingY != 0) {
        if (!g_mouseYawScaleCalibrated) {
            g_mousePreviewNoScale.fetch_add(1, std::memory_order_relaxed);
        } else {
            const float pitchScale =
                g_mouseInvert ? -g_mouseYawPerRoundedPixel : g_mouseYawPerRoundedPixel;
            if (mouse_preview::ContinuousAngle(
                    pendingX, g_mouseSensitivity, g_mouseYawPerRoundedPixel,
                    diagnostics.pendingYaw) &&
                mouse_preview::ContinuousAngle(
                    pendingY, g_mouseSensitivity, pitchScale,
                    diagnostics.pendingPitch)) {
                diagnostics.pendingScaleValid = true;
                yaw += diagnostics.pendingYaw;
                pitch += diagnostics.pendingPitch;
                g_mousePreviewPendingX.fetch_add(pendingX, std::memory_order_relaxed);
                g_mousePreviewPendingY.fetch_add(pendingY, std::memory_order_relaxed);
            } else {
                g_mousePreviewNoScale.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    if (std::fabs(pitch) < 0.0000001F && std::fabs(yaw) < 0.0000001F) {
        return false;
    }
    camera::Pose before{};
    camera::Pose after{};
    if (g_cameraRuntime.ApplyAngularOffset(pitch, yaw, &before, &after)) {
        diagnostics.applied = true;
        g_mousePreviewApplied.fetch_add(1, std::memory_order_relaxed);
        constexpr float kPi = 3.14159265358979323846F;
        if (std::abs(after.yaw - before.yaw) > kPi) {
            g_mousePreviewYawRebased.fetch_add(1, std::memory_order_relaxed);
        }
        if (BuildCameraPresentationCorrection(before, after, handsCorrection)) {
            g_mouseHandsCorrectionBuilt.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        g_mouseHandsCorrectionFailed.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_mousePreviewApplyFailed.fetch_add(1, std::memory_order_relaxed);
    }
    return false;
}

void RecordClockReset(const clock::ResetReason reason) noexcept {
    if (reason == clock::ResetReason::none) {
        return;
    }
    g_lastClockReset.store(reason, std::memory_order_relaxed);
    g_clockResets.fetch_add(1, std::memory_order_relaxed);
}

void RecordClockSample(const clock::Sample& sample) noexcept {
    if (sample.phaseRebased) {
        g_clockPhaseRebases.fetch_add(1, std::memory_order_relaxed);
    }
    switch (sample.kind) {
    case clock::SampleKind::unavailable:
        g_clockUnavailable.fetch_add(1, std::memory_order_relaxed);
        break;
    case clock::SampleKind::interpolated:
        g_clockInterpolated.fetch_add(1, std::memory_order_relaxed);
        break;
    case clock::SampleKind::clampedToOldest:
        g_clockClampedOldest.fetch_add(1, std::memory_order_relaxed);
        break;
    case clock::SampleKind::clampedToNewest:
        g_clockClampedNewest.fetch_add(1, std::memory_order_relaxed);
        break;
    case clock::SampleKind::reset:
        RecordClockReset(sample.resetReason);
        break;
    }
    const double boundedAlpha = sample.alpha < 0.0 ? 0.0 : sample.alpha > 1.0 ? 1.0 : sample.alpha;
    g_latestAlphaMillionths.store(static_cast<std::uint32_t>(boundedAlpha * 1'000'000.0 + 0.5),
                                  std::memory_order_relaxed);
}

void RecordCameraResult(const camera::FrameResult result) noexcept {
    g_lastCameraResult.store(result, std::memory_order_relaxed);
    switch (result) {
    case camera::FrameResult::captured:
        g_cameraCaptured.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::applied:
        g_cameraApplied.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::noScene:
        g_cameraNoScene.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::noCurrentListener:
        g_cameraNoView.fetch_add(1, std::memory_order_relaxed);
        g_cameraNoListener.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::listenerHidden:
        g_cameraNoView.fetch_add(1, std::memory_order_relaxed);
        g_cameraListenerHidden.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::listenerMissingCamera:
        g_cameraNoView.fetch_add(1, std::memory_order_relaxed);
        g_cameraListenerMissingCamera.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::listenerMissingWorld:
        g_cameraNoView.fetch_add(1, std::memory_order_relaxed);
        g_cameraListenerMissingWorld.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::invalidViewport:
        g_cameraNoView.fetch_add(1, std::memory_order_relaxed);
        g_cameraInvalidViewport.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::identityChanged:
        g_cameraIdentityChanges.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::clockGenerationReset:
        g_cameraGenerationResets.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::nonMonotonicReset:
        g_cameraNonMonotonicResets.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::sameTickMutation:
        g_cameraSameTickMutations.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::discontinuityReset:
        g_cameraDiscontinuityResets.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::clockUnavailable:
        g_cameraClockWaits.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::snapshotsMissing:
        g_cameraSnapshotMisses.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::invalidCamera:
        g_cameraInvalid.fetch_add(1, std::memory_order_relaxed);
        break;
    case camera::FrameResult::applyFailed:
        g_cameraApplyFailures.fetch_add(1, std::memory_order_relaxed);
        break;
    }
}

void RecordHandsResult(const hands::FrameResult result) noexcept {
    g_lastHandsResult.store(result, std::memory_order_relaxed);
    switch (result) {
    case hands::FrameResult::staged:
        g_handsStaged.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::FrameResult::captured:
        g_handsCaptured.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::FrameResult::renderReady:
        g_handsRenderReady.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::FrameResult::unavailable:
        g_handsUnavailable.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::FrameResult::identityChanged:
        g_handsIdentityChanges.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::FrameResult::clockGenerationReset:
    case hands::FrameResult::nonMonotonicReset:
    case hands::FrameResult::sameTickMutation:
    case hands::FrameResult::discontinuityReset:
        g_handsResets.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::FrameResult::clockUnavailable:
        g_handsClockWaits.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::FrameResult::snapshotsMissing:
        g_handsSnapshotMisses.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::FrameResult::invalid:
        g_handsInvalid.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::FrameResult::applyFailed:
        g_handsApplyFailures.fetch_add(1, std::memory_order_relaxed);
        break;
    }
}

void RecordHandAttachments(const hands::AttachmentBatch& result) noexcept {
    g_handAttachmentCandidates.fetch_add(result.candidates, std::memory_order_relaxed);
    g_handAttachmentApplied.fetch_add(result.applied, std::memory_order_relaxed);
    g_handAttachmentParentMismatch.fetch_add(result.parentMismatch,
                                             std::memory_order_relaxed);
    g_handAttachmentInvalid.fetch_add(result.invalid, std::memory_order_relaxed);
    g_handAttachmentCapacity.fetch_add(result.capacityExhausted,
                                       std::memory_order_relaxed);
}

void RecordWorldStageResult(const world::StageResult result) noexcept {
    switch (result) {
    case world::StageResult::stagedProp:
        g_worldPropsStaged.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::StageResult::stagedGrunt:
        g_worldGruntsStaged.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::StageResult::cachedSleepingProp:
        g_worldCachedSleeping.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::StageResult::invalid:
        g_worldInvalid.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::StageResult::capacityExhausted:
        g_worldCapacityExhausted.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::StageResult::ineligible:
        break;
    }
}

void RecordWorldBatch(const world::BatchResult& result) noexcept {
    g_worldCaptured.fetch_add(result.captured, std::memory_order_relaxed);
    g_worldSleeping.fetch_add(result.sleeping, std::memory_order_relaxed);
    g_worldMovedCallbacks.fetch_add(result.movedCallbacks, std::memory_order_relaxed);
    g_worldMovedMatched.fetch_add(result.movedMatched, std::memory_order_relaxed);
    g_worldMovedStandalone.fetch_add(result.movedStandalone, std::memory_order_relaxed);
    g_worldMovedOverflow.fetch_add(result.movedOverflow, std::memory_order_relaxed);
    g_worldMultiBodyOwners.fetch_add(result.multiBodyOwners, std::memory_order_relaxed);
    g_worldModelCandidates.fetch_add(result.modelCandidates, std::memory_order_relaxed);
    g_worldModelCaptured.fetch_add(result.modelCaptured, std::memory_order_relaxed);
    g_worldModelInvalid.fetch_add(result.modelInvalid, std::memory_order_relaxed);
    g_worldModelCapacity.fetch_add(result.modelCapacity, std::memory_order_relaxed);
    g_worldModelReady.fetch_add(result.modelReady, std::memory_order_relaxed);
    g_worldModelMovingReady.fetch_add(result.modelMovingReady, std::memory_order_relaxed);
    g_worldModelCacheApplied.fetch_add(result.modelCacheApplied, std::memory_order_relaxed);
    g_worldModelCacheInvalid.fetch_add(result.modelCacheInvalid, std::memory_order_relaxed);
    g_worldModelRestoreFailed.fetch_add(result.modelRestoreFailed, std::memory_order_relaxed);
    g_worldReady.fetch_add(result.ready, std::memory_order_relaxed);
    g_worldResets.fetch_add(result.reset, std::memory_order_relaxed);
    g_worldPairMisses.fetch_add(result.pairMiss, std::memory_order_relaxed);
    g_worldInvalid.fetch_add(result.invalid, std::memory_order_relaxed);
    g_worldApplyFailures.fetch_add(result.applyFailed, std::memory_order_relaxed);
}

void RecordWorldModelHistoryGate(const world::ModelHistoryGate gate) noexcept {
    if (gate == world::ModelHistoryGate::notFound) {
        return;
    }
    g_worldModelHistorySeen.fetch_add(1, std::memory_order_relaxed);
    switch (gate) {
    case world::ModelHistoryGate::bodyPriority:
        g_worldModelHistoryBodyPriority.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::ModelHistoryGate::ownerReadInvalid:
        g_worldModelHistoryOwnerInvalid.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::ModelHistoryGate::ownerMissing:
        g_worldModelHistoryOwnerMissing.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::ModelHistoryGate::ownerNotReady:
        g_worldModelHistoryOwnerNotReady.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::ModelHistoryGate::staleSerial:
        g_worldModelHistoryStale.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::ModelHistoryGate::clockUnavailable:
        g_worldModelHistoryClockWait.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::ModelHistoryGate::generationMismatch:
        g_worldModelHistoryGeneration.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::ModelHistoryGate::snapshotsMissing:
        g_worldModelHistoryPairMiss.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::ModelHistoryGate::sampleInvalid:
        g_worldModelHistoryInvalid.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::ModelHistoryGate::appliedMovingUnchanged:
        g_worldModelHistoryMovingUnchanged.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::ModelHistoryGate::appliedMovingChanged:
        g_worldModelHistoryMovingChanged.fetch_add(1, std::memory_order_relaxed);
        break;
    case world::ModelHistoryGate::notFound:
    case world::ModelHistoryGate::slotsExhausted:
    case world::ModelHistoryGate::applied:
        break;
    }
}

void RecordPoseStage(const pose::StageResult result) noexcept {
    switch (result) {
    case pose::StageResult::staged:
        g_poseStaged.fetch_add(1, std::memory_order_relaxed);
        break;
    case pose::StageResult::unavailable:
        g_poseUnavailable.fetch_add(1, std::memory_order_relaxed);
        break;
    case pose::StageResult::inactive:
        break;
    }
}

void RecordPoseUpdate(const pose::UpdateResult result) noexcept {
    switch (result) {
    case pose::UpdateResult::applied:
        g_poseApplied.fetch_add(1, std::memory_order_relaxed);
        break;
    case pose::UpdateResult::alreadyApplied:
        g_poseAlreadyApplied.fetch_add(1, std::memory_order_relaxed);
        break;
    case pose::UpdateResult::unavailable:
        g_poseUnavailable.fetch_add(1, std::memory_order_relaxed);
        break;
    case pose::UpdateResult::identityChanged:
        g_poseIdentityChanges.fetch_add(1, std::memory_order_relaxed);
        break;
    case pose::UpdateResult::clockUnavailable:
        g_poseClockWaits.fetch_add(1, std::memory_order_relaxed);
        break;
    case pose::UpdateResult::pairMissing:
        g_posePairMisses.fetch_add(1, std::memory_order_relaxed);
        break;
    case pose::UpdateResult::invalid:
        g_poseInvalid.fetch_add(1, std::memory_order_relaxed);
        break;
    case pose::UpdateResult::notTarget:
    case pose::UpdateResult::captured:
        break;
    }
}

void RecordEnemyPoseStage(const enemy_pose::StageResult result) noexcept {
    switch (result) {
    case enemy_pose::StageResult::stagedEnemy:
        g_enemyPoseEnemyStaged.fetch_add(1, std::memory_order_relaxed);
        break;
    case enemy_pose::StageResult::stagedRagdoll:
        g_enemyPoseRagdollStaged.fetch_add(1, std::memory_order_relaxed);
        break;
    case enemy_pose::StageResult::invalid:
        g_enemyPoseInvalid.fetch_add(1, std::memory_order_relaxed);
        break;
    case enemy_pose::StageResult::capacityExhausted:
        g_enemyPoseCapacityExhausted.fetch_add(1, std::memory_order_relaxed);
        break;
    case enemy_pose::StageResult::ineligible:
        break;
    }
}

void RecordEnemyPoseBatch(const enemy_pose::BatchResult& result) noexcept {
    g_enemyPoseTargets.fetch_add(result.targets, std::memory_order_relaxed);
    g_enemyPoseVisibilitySkipped.fetch_add(result.visibilitySkipped,
                                           std::memory_order_relaxed);
    g_enemyPoseVisibilityUnavailable.fetch_add(result.visibilityUnavailable,
                                               std::memory_order_relaxed);
    g_enemyPoseCaptured.fetch_add(result.captured, std::memory_order_relaxed);
    g_enemyPoseApplied.fetch_add(result.applied, std::memory_order_relaxed);
    g_enemyPoseIdentityChanges.fetch_add(result.identityChanged, std::memory_order_relaxed);
    g_enemyPoseGenerationResets.fetch_add(result.generationReset, std::memory_order_relaxed);
    g_enemyPoseNonMonotonicResets.fetch_add(result.nonMonotonicReset, std::memory_order_relaxed);
    g_enemyPoseSameTickMutations.fetch_add(result.sameTickMutation, std::memory_order_relaxed);
    g_enemyPoseBoneCountResets.fetch_add(result.boneCountReset, std::memory_order_relaxed);
    g_enemyPoseDiscontinuityResets.fetch_add(result.discontinuityReset, std::memory_order_relaxed);
    g_enemyPoseClockWaits.fetch_add(result.clockWait, std::memory_order_relaxed);
    g_enemyPosePairMisses.fetch_add(result.pairMissing, std::memory_order_relaxed);
    g_enemyPoseInvalid.fetch_add(result.invalid, std::memory_order_relaxed);
    g_enemyPoseUnavailable.fetch_add(result.unavailable, std::memory_order_relaxed);
    g_enemyPoseRestoreFailures.fetch_add(result.restoreFailed, std::memory_order_relaxed);
}

const wchar_t* RenderUnlockStateName(const RenderUnlockState state) {
    switch (state) {
    case RenderUnlockState::disabled:
        return L"disabled";
    case RenderUnlockState::pending:
        return L"pending";
    case RenderUnlockState::active:
        return L"active";
    case RenderUnlockState::rejected:
        return L"rejected";
    case RenderUnlockState::finished:
        return L"finished";
    }
    return L"unknown";
}

void __fastcall EngineRunDetour(void* self, void*) {
    if (self == nullptr) {
        if (g_renderUnlockEnabled) {
            g_renderUnlockState.store(RenderUnlockState::rejected,
                                      std::memory_order_release);
        }
        log::Write(log::Level::error,
                   L"cEngine::Run received a null instance; presentation timing is "
                   L"unavailable.");
        g_originalEngineRun(self);
        return;
    }

    if (g_interpolationClockEnabled) {
        const auto* engineBytes = static_cast<const std::uint8_t*>(self);
        const void* timerField = engineBytes + kEngineLogicTimerOffset;
        void* logicTimer = nullptr;
        if (IsReadableMemory(timerField, sizeof(logicTimer))) {
            std::memcpy(&logicTimer, timerField, sizeof(logicTimer));
        }
        if (IsReadableMemory(logicTimer, kLogicTimerReadableBytes) &&
            g_sdlGetTicks != nullptr) {
            g_logicTimer = logicTimer;
            const double qpcMilliseconds = QpcSeconds() * 1000.0;
            g_sdlQpcOffsetMilliseconds =
                static_cast<double>(g_sdlGetTicks()) - qpcMilliseconds;
            g_sdlQpcOffsetValid = true;
            log::Write(log::Level::info,
                       L"HPL logic-timer residual acquired from cEngine::mpLogicTimer; "
                       L"presentation retains only previous/current fixed states.");
        } else {
            g_logicTimer = nullptr;
            g_sdlQpcOffsetValid = false;
            log::Write(log::Level::error,
                       L"HPL logic-timer layout validation failed; interpolation will "
                       L"fail closed.");
        }
    }

    if (!g_renderUnlockEnabled) {
        g_originalEngineRun(self);
        g_logicTimer = nullptr;
        g_sdlQpcOffsetValid = false;
        return;
    }

    auto* const limitFps = reinterpret_cast<std::uint8_t*>(self) + kEngineLimitFpsOffset;
    const std::uint8_t originalValue = *limitFps;
    if (originalValue > 1U) {
        g_renderUnlockState.store(RenderUnlockState::rejected, std::memory_order_release);
        log::Write(log::Level::error,
                   L"Render unlock rejected: cEngine::mbLimitFPS failed its runtime "
                   L"boolean layout check.");
        g_originalEngineRun(self);
        return;
    }

    *limitFps = 0;
    g_renderUnlockState.store(RenderUnlockState::active, std::memory_order_release);
    log::Write(log::Level::info, L"Render unlock applied at cEngine::Run: "
                                 L"mbLimitFPS=false; the fixed-step "
                                 L"logic timer is unchanged.");
    g_originalEngineRun(self);
    g_logicTimer = nullptr;
    g_sdlQpcOffsetValid = false;
    *limitFps = originalValue;
    g_renderUnlockState.store(RenderUnlockState::finished, std::memory_order_release);
}

void __fastcall PlayerHandsPostUpdateDetour(void* self, void*, const float timeStep) {
    g_originalPlayerHandsPostUpdate(self, timeStep);
    if (g_handsInterpolationEnabled) {
        RecordHandsResult(g_handsRuntime.Stage(self));
    }
    if (g_handsPoseInterpolationEnabled) {
        RecordPoseStage(
            g_poseRuntime.StageHands(self, g_tickSerial.load(std::memory_order_relaxed) + 1));
    }
}

void __fastcall LuxPropOnUpdateDetour(void* self, void*, const float timeStep) {
    g_originalLuxPropOnUpdate(self, timeStep);
    const std::uint64_t pendingSerial = g_tickSerial.load(std::memory_order_relaxed) + 1;
    world::PropStageInfo stageInfo{};
    const world::StageResult worldResult =
        g_worldRuntime.StageProp(self, pendingSerial, &stageInfo);
    RecordWorldStageResult(worldResult);
    // cachedSleepingProp is intentionally excluded: the world runtime only
    // emits that result for validated single-body sleepers. Only fully staged
    // multi-body props can require a physics-driven bone palette.
    if (g_worldPoseInterpolationEnabled && g_ragdollPoseInterpolationEnabled &&
        worldResult == world::StageResult::stagedProp && stageInfo.valid &&
        stageInfo.bodyCount >= 2) {
        RecordEnemyPoseStage(g_enemyPoseRuntime.StageRagdollProp(
            self, stageInfo.mesh, stageInfo.id, stageInfo.bodyCount,
            stageInfo.allSleeping, pendingSerial));
    }
}

void __fastcall LuxEnemyOnUpdateDetour(void* self, void*, const float timeStep) {
    g_originalLuxEnemyOnUpdate(self, timeStep);
    const std::uint64_t pendingSerial = g_tickSerial.load(std::memory_order_relaxed) + 1;
    if (g_worldInterpolationEnabled) {
        RecordWorldStageResult(g_worldRuntime.StageEnemy(self, pendingSerial));
    }
    if (g_worldPoseInterpolationEnabled) {
        RecordEnemyPoseStage(g_enemyPoseRuntime.StageEnemy(self, pendingSerial));
    }
}

void __cdecl NewtonTransformCallbackDetour(void* newtonBody, const float* matrix,
                                           const int threadIndex) {
    g_originalNewtonTransformCallback(newtonBody, matrix, threadIndex);
    if (g_worldInterpolationEnabled && g_newtonBodyGetUserData != nullptr) {
        g_worldRuntime.NotifyBodyMoved(g_newtonBodyGetUserData(newtonBody));
    }
}

hands::Matrix* __fastcall SubMeshGetModelMatrixDetour(void* self, void*, void* frustum) {
    hands::Matrix* original = g_originalSubMeshGetModelMatrix(self, frustum);
    const hands::Matrix* result = original;
    const hands::OverrideResult overrideResult =
        g_handsRuntime.OverrideModelMatrix(self, original, result);
    switch (overrideResult) {
    case hands::OverrideResult::applied:
        g_handsModelOverrides.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::OverrideResult::appliedHandObject:
        g_handObjectModelOverrides.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::OverrideResult::appliedHandObjectHistory:
        g_handObjectModelOverrides.fetch_add(1, std::memory_order_relaxed);
        g_handObjectHistoryOverrides.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::OverrideResult::invalid:
        g_handsInvalid.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::OverrideResult::slotsExhausted:
        g_handsSlotExhausted.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::OverrideResult::notHands:
        break;
    }
    if (overrideResult == hands::OverrideResult::notHands && g_worldInterpolationEnabled) {
        world::ModelHistoryGate historyGate = world::ModelHistoryGate::notFound;
        const world::OverrideResult worldResult =
            g_worldRuntime.OverrideModelMatrix(self, original, result, &historyGate);
        RecordWorldModelHistoryGate(historyGate);
        switch (worldResult) {
        case world::OverrideResult::appliedProp:
            g_worldPropModels.fetch_add(1, std::memory_order_relaxed);
            break;
        case world::OverrideResult::appliedPropParent:
            g_worldPropModels.fetch_add(1, std::memory_order_relaxed);
            g_worldPropParentModels.fetch_add(1, std::memory_order_relaxed);
            break;
        case world::OverrideResult::appliedPropNullModel:
            g_worldPropModels.fetch_add(1, std::memory_order_relaxed);
            g_worldPropNullModels.fetch_add(1, std::memory_order_relaxed);
            break;
        case world::OverrideResult::appliedPropParentNullModel:
            g_worldPropModels.fetch_add(1, std::memory_order_relaxed);
            g_worldPropParentModels.fetch_add(1, std::memory_order_relaxed);
            g_worldPropNullModels.fetch_add(1, std::memory_order_relaxed);
            break;
        case world::OverrideResult::appliedPropDirectParent:
            g_worldPropModels.fetch_add(1, std::memory_order_relaxed);
            g_worldPropParentModels.fetch_add(1, std::memory_order_relaxed);
            g_worldPropDirectParentModels.fetch_add(1, std::memory_order_relaxed);
            break;
        case world::OverrideResult::appliedPropDirectParentNullModel:
            g_worldPropModels.fetch_add(1, std::memory_order_relaxed);
            g_worldPropParentModels.fetch_add(1, std::memory_order_relaxed);
            g_worldPropNullModels.fetch_add(1, std::memory_order_relaxed);
            g_worldPropDirectParentModels.fetch_add(1, std::memory_order_relaxed);
            break;
        case world::OverrideResult::appliedPropModelHistory:
            g_worldPropModels.fetch_add(1, std::memory_order_relaxed);
            g_worldPropModelHistoryModels.fetch_add(1, std::memory_order_relaxed);
            break;
        case world::OverrideResult::appliedGrunt:
            g_worldGruntModels.fetch_add(1, std::memory_order_relaxed);
            break;
        case world::OverrideResult::appliedStandaloneBody:
            g_worldStandaloneModels.fetch_add(1, std::memory_order_relaxed);
            break;
        case world::OverrideResult::appliedStandaloneBodyNullModel:
            g_worldStandaloneModels.fetch_add(1, std::memory_order_relaxed);
            g_worldStandaloneNullModels.fetch_add(1, std::memory_order_relaxed);
            break;
        case world::OverrideResult::propParentMismatch:
            g_worldParentMisses.fetch_add(1, std::memory_order_relaxed);
            break;
        case world::OverrideResult::invalid:
            g_worldInvalid.fetch_add(1, std::memory_order_relaxed);
            break;
        case world::OverrideResult::slotsExhausted:
            g_worldModelSlotsExhausted.fetch_add(1, std::memory_order_relaxed);
            break;
        case world::OverrideResult::notWorld:
            break;
        }
    }
    return const_cast<hands::Matrix*>(result);
}

void __fastcall MouseSdlUpdateDetour(void* self, void*) {
    if (self == nullptr) {
        g_originalMouseSdlUpdate(self);
        return;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(self);
    const auto previousX = *reinterpret_cast<const std::int32_t*>(bytes + 0x30);
    const auto previousY = *reinterpret_cast<const std::int32_t*>(bytes + 0x34);
    if (previousX != 0 || previousY != 0) {
        g_mouseUnconsumedOverwrites.fetch_add(1, std::memory_order_relaxed);
    }

    g_originalMouseSdlUpdate(self);

    g_mouseDevice = self;
    g_mouseThreadId = GetCurrentThreadId();
    (void)EnsureRawMouseTapInstalled();
    auto* writableBytes = static_cast<std::uint8_t*>(self);
    const std::int64_t pendingX =
        g_mousePendingX.exchange(0, std::memory_order_relaxed);
    const std::int64_t pendingY =
        g_mousePendingY.exchange(0, std::memory_order_relaxed);
    const auto freshX = *reinterpret_cast<const std::int32_t*>(bytes + 0x30);
    const auto freshY = *reinterpret_cast<const std::int32_t*>(bytes + 0x34);
    const std::uint64_t tick = g_tickSerial.load(std::memory_order_relaxed);
    const std::uint64_t lastGameplayTick =
        g_mouseLastGameplayTick.load(std::memory_order_relaxed);
    const bool previewEligible = g_mousePresentationAcquisitionEnabled &&
        g_mousePresentationAcquisitionEligible.load(std::memory_order_relaxed) &&
        lastGameplayTick != 0 && tick <= lastGameplayTick + 1;
    const bool rawMouseActive = g_rawMouseActive.load(std::memory_order_acquire);
    mouse_preview::TickRoute routeX{};
    mouse_preview::TickRoute routeY{};
    if (rawMouseActive) {
        // SDL's relative state contains the same WM_INPUT reports, accumulated
        // according to when SDL was pumped. Use the individual packet stream as
        // the sole movement source and retain SDL only for non-motion behavior.
        routeX.simulationCounts = previewEligible ? pendingX : freshX;
        routeY.simulationCounts = previewEligible ? pendingY : freshY;
        if (previewEligible) {
            g_rawMouseTickComparisons.fetch_add(1, std::memory_order_relaxed);
            if (pendingX != freshX || pendingY != freshY) {
                g_rawMouseTickMismatches.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            // Do not carry cursor/menu motion across the next gameplay
            // transition as a presentation-only relative delta.
            g_rawMouseSinceRenderX.store(0, std::memory_order_relaxed);
            g_rawMouseSinceRenderY.store(0, std::memory_order_relaxed);
        }
    } else {
        routeX = mouse_preview::RouteTickCounts(freshX, pendingX, previewEligible);
        routeY = mouse_preview::RouteTickCounts(freshY, pendingY, previewEligible);
    }
    const auto x = SaturatingAddMouse(0, routeX.simulationCounts);
    const auto y = SaturatingAddMouse(0, routeY.simulationCounts);
    *reinterpret_cast<std::int32_t*>(writableBytes + 0x30) = x;
    *reinterpret_cast<std::int32_t*>(writableBytes + 0x34) = y;
    if (previewEligible && !rawMouseActive) {
        g_mousePendingX.fetch_add(routeX.deferredCounts, std::memory_order_relaxed);
        g_mousePendingY.fetch_add(routeY.deferredCounts, std::memory_order_relaxed);
        g_mouseTickDeferredPolls.fetch_add(1, std::memory_order_relaxed);
        if (freshX != 0 || freshY != 0) {
            g_mouseTickDeferredNonZero.fetch_add(1, std::memory_order_relaxed);
        }
        g_mouseTickDeferredX.fetch_add(routeX.deferredCounts, std::memory_order_relaxed);
        g_mouseTickDeferredY.fetch_add(routeY.deferredCounts, std::memory_order_relaxed);
        g_mouseDeferredSinceRenderX.fetch_add(routeX.deferredCounts,
                                              std::memory_order_relaxed);
        g_mouseDeferredSinceRenderY.fetch_add(routeY.deferredCounts,
                                              std::memory_order_relaxed);
    }
    if (pendingX != 0 || pendingY != 0) {
        g_mousePendingDeliveries.fetch_add(1, std::memory_order_relaxed);
    }
    g_mousePendingDeliveredX.fetch_add(pendingX, std::memory_order_relaxed);
    g_mousePendingDeliveredY.fetch_add(pendingY, std::memory_order_relaxed);
    g_mouseUpdates.fetch_add(1, std::memory_order_relaxed);
    if (x != 0 || y != 0) {
        g_mouseAcquiredNonZero.fetch_add(1, std::memory_order_relaxed);
    }
    g_mouseAcquiredX.fetch_add(x, std::memory_order_relaxed);
    g_mouseAcquiredY.fetch_add(y, std::memory_order_relaxed);
}

void __fastcall LuxPlayerAddYawDetour(void* self, void*, const float amount) {
    float adjusted = amount;
    g_playerYawDequantizedThisCall = DequantizePlayerLookAmount(
        amount, g_playerMouseXThisCall, false, adjusted);
    g_originalLuxPlayerAddYaw(self, adjusted);
}

void __fastcall LuxPlayerAddPitchDetour(void* self, void*, const float amount) {
    float adjusted = amount;
    g_playerPitchDequantizedThisCall = DequantizePlayerLookAmount(
        amount, g_playerMouseYThisCall, true, adjusted);
    g_originalLuxPlayerAddPitch(self, adjusted);
}

void __fastcall CameraAddPitchDetour(void* self, void*, const float amount) {
    const float before = self == nullptr
                             ? 0.0F
                             : *reinterpret_cast<const float*>(
                                   static_cast<const std::uint8_t*>(self) + 0x28);
    g_originalCameraAddPitch(self, amount);
    if (self != nullptr) {
        const float after = *reinterpret_cast<const float*>(
            static_cast<const std::uint8_t*>(self) + 0x28);
        CaptureCameraAngularDelta(self, after - before, true);
    }
}

void __fastcall CameraAddYawDetour(void* self, void*, const float amount) {
    const float before = self == nullptr
                             ? 0.0F
                             : *reinterpret_cast<const float*>(
                                   static_cast<const std::uint8_t*>(self) + 0x2C);
    g_originalCameraAddYaw(self, amount);
    if (self != nullptr) {
        const float after = *reinterpret_cast<const float*>(
            static_cast<const std::uint8_t*>(self) + 0x2C);
        CaptureCameraAngularDelta(self, after - before, false);
    }
}

MouseDelta* __fastcall MouseSdlGetRelPositionDetour(void* self, void*, MouseDelta* result) {
    MouseDelta* const returned = g_originalMouseSdlGetRelPosition(self, result);
    g_mouseReads.fetch_add(1, std::memory_order_relaxed);
    if (g_inGamePlayerInput) {
        ++g_playerMouseReadsThisCall;
        if (result != nullptr) {
            g_playerMouseXThisCall = result->x;
            g_playerMouseYThisCall = result->y;
        }
        g_mouseGameplayReads.fetch_add(1, std::memory_order_relaxed);
    }
    if (result == nullptr) {
        g_mouseStateInvalid.fetch_add(1, std::memory_order_relaxed);
        return returned;
    }
    if (returned != result) {
        g_mouseStateInvalid.fetch_add(1, std::memory_order_relaxed);
    }
    if (result->x != 0 || result->y != 0) {
        g_mouseRetiredNonZero.fetch_add(1, std::memory_order_relaxed);
    }
    g_mouseRetiredX.fetch_add(result->x, std::memory_order_relaxed);
    g_mouseRetiredY.fetch_add(result->y, std::memory_order_relaxed);
    return returned;
}

void __fastcall LuxUpdateGamePlayerInputDetour(void* self, void*) {
    g_mousePlayerCalls.fetch_add(1, std::memory_order_relaxed);
    if (self == nullptr) {
        g_mouseStateInvalid.fetch_add(1, std::memory_order_relaxed);
        g_originalLuxUpdateGamePlayerInput(self);
        return;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(self);
    const std::uint8_t smooth = *(bytes + 0x40);
    const std::uint8_t invert = *(bytes + 0x41);
    const float sensitivity = *reinterpret_cast<const float*>(bytes + 0x50);
    if (smooth > 1U || invert > 1U || !(sensitivity > 0.0F && sensitivity <= 100.0F)) {
        g_mouseStateInvalid.fetch_add(1, std::memory_order_relaxed);
    }
    if (smooth == 0U) {
        g_mouseSmoothOff.fetch_add(1, std::memory_order_relaxed);
    } else if (smooth == 1U) {
        g_mouseSmoothOn.fetch_add(1, std::memory_order_relaxed);
    }

    g_playerMouseReadsThisCall = 0;
    g_playerMouseXThisCall = 0;
    g_playerMouseYThisCall = 0;
    g_playerCameraThisCall = nullptr;
    g_playerCameraPitchThisCall = 0.0F;
    g_playerCameraYawThisCall = 0.0F;
    g_playerCameraPitchCallsThisCall = 0;
    g_playerCameraYawCallsThisCall = 0;
    g_playerMouseSensitivityThisCall = sensitivity;
    g_playerMouseInvertThisCall = invert != 0U;
    g_playerContinuousLookThisCall =
        g_mousePresentationAcquisitionEnabled && smooth == 0U;
    g_playerYawDequantizedThisCall = false;
    g_playerPitchDequantizedThisCall = false;
    g_inGamePlayerInput = true;
    g_originalLuxUpdateGamePlayerInput(self);
    g_inGamePlayerInput = false;
    if (g_playerMouseReadsThisCall == 0U) {
        g_mousePlayerEarlyExits.fetch_add(1, std::memory_order_relaxed);
    } else if (g_playerMouseReadsThisCall > 1U) {
        g_mousePlayerMultipleReads.fetch_add(1, std::memory_order_relaxed);
    }
    const bool eligible = smooth == 0U && g_playerMouseReadsThisCall == 1U;
    g_mousePresentationAcquisitionEligible.store(eligible, std::memory_order_relaxed);
    g_mouseLastGameplayTick.store(g_tickSerial.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
    if (eligible) {
        PublishMouseAngularCommand(sensitivity, invert != 0U);
    }
}

void __fastcall UpdaterRunMessageDetour(void* self, void*, const int message, const float value) {
    // GuiSet::DrawMouse queues the software cursor while OnDraw listeners run.
    // Present pending native motion before that queue is built; applying it
    // later from cScene::Render leaves the stock main/pause cursor one fixed
    // update behind even though inventory-specific drawing can appear current.
    if (message == 2) { // eUpdateableMessage_OnDraw
        ApplyGuiMousePresentation();
    }
    g_originalUpdaterRunMessage(self, message, value);
    switch (message) {
    case 2: // eUpdateableMessage_OnDraw
        g_onDrawMessages.fetch_add(1, std::memory_order_relaxed);
        break;
    case 3: // eUpdateableMessage_OnPostRender
        g_postRenderMessages.fetch_add(1, std::memory_order_relaxed);
        break;
    case 6: // eUpdateableMessage_PostUpdate; count only after every listener
            // returned.
        g_completedTicks.fetch_add(1, std::memory_order_relaxed);
        {
            const std::uint64_t serial = g_tickSerial.fetch_add(1, std::memory_order_relaxed) + 1;
            const clock::PublishResult published =
                g_presentationClock.Publish(serial, QpcSeconds());
            RecordClockReset(published.resetReason);
            if (g_cameraInterpolationEnabled) {
                RecordCameraResult(
                    g_cameraRuntime.CaptureCompletedTick(serial, g_presentationClock.Generation()));
            }
            if (g_handsInterpolationEnabled) {
                RecordHandsResult(
                    g_handsRuntime.CommitCompletedTick(serial, g_presentationClock.Generation()));
            }
            if (g_worldInterpolationEnabled) {
                const world::BatchResult worldResult =
                    g_worldRuntime.CommitCompletedTick(serial, g_presentationClock.Generation());
                RecordWorldBatch(worldResult);
            }
            if (g_handsPoseInterpolationEnabled) {
                g_poseRuntime.CommitCompletedTick(serial, g_presentationClock.Generation());
            }
            if (g_worldPoseInterpolationEnabled) {
                g_enemyPoseRuntime.CommitCompletedTick(serial, g_presentationClock.Generation());
            }
        }
        break;
    default:
        break;
    }
}

void __fastcall SceneRenderDetour(void* self, void*, const float frameTime,
                                  const std::uint32_t flags) {
    LARGE_INTEGER presentationCounter{};
    QueryPerformanceCounter(&presentationCounter);
    g_frameCadence.RecordQpc(presentationCounter.QuadPart, g_qpcFrequency.QuadPart);
    const PresentationMousePoll presentationPoll = AcquirePresentationMouse();
    const std::int64_t deferredSinceRenderX =
        g_mouseDeferredSinceRenderX.exchange(0, std::memory_order_relaxed);
    const std::int64_t deferredSinceRenderY =
        g_mouseDeferredSinceRenderY.exchange(0, std::memory_order_relaxed);
    const double presentationSeconds =
        static_cast<double>(presentationCounter.QuadPart) /
        static_cast<double>(g_qpcFrequency.QuadPart);
    double presentationDelayTicks = 0.0;
    bool presentationIntervalValid = false;
    if (g_previousPresentationValid) {
        const double presentationInterval =
            presentationSeconds - g_previousPresentationSeconds;
        if (std::isfinite(presentationInterval) && presentationInterval > 0.0 &&
            presentationInterval <= clock::PresentationClock::kLongStallSeconds) {
            presentationDelayTicks = std::clamp(
                presentationInterval / clock::PresentationClock::kFixedStepSeconds,
                0.0, 1.0) * kPresentationHeadroomFraction;
            presentationIntervalValid = true;
        }
    }
    g_previousPresentationSeconds = presentationSeconds;
    g_previousPresentationValid = true;
    double interpolationAlpha = 0.0;
    double rawInterpolationAlpha = 0.0;
    const bool phaseValid = presentationIntervalValid &&
        ReadLogicTimerAlpha(presentationSeconds, presentationDelayTicks,
                            interpolationAlpha, rawInterpolationAlpha);
    const clock::Sample clockSample = g_presentationClock.Evaluate(
        presentationSeconds, interpolationAlpha, phaseValid);
    RecordClockSample(clockSample);

    bool cameraApplied = false;
    camera::Pose authoritativePose{};
    camera::Pose basePose{};
    camera::Pose finalPose{};
    bool authoritativePoseValid = false;
    bool basePoseValid = false;
    bool finalPoseValid = false;
    hands::Matrix mouseHandsCorrection = hands::Identity();
    bool mouseHandsCorrectionReady = false;
    MousePreviewDiagnostics mousePreview{};
    const std::uint64_t currentSerial = g_tickSerial.load(std::memory_order_relaxed);
    if (g_cameraInterpolationEnabled) {
        const camera::FrameResult cameraResult =
            g_cameraRuntime.Apply(self, currentSerial, clockSample);
        RecordCameraResult(cameraResult);
        cameraApplied = cameraResult == camera::FrameResult::applied;
    }
    if (cameraApplied) {
        authoritativePoseValid = g_cameraRuntime.ReadAuthoritativePose(authoritativePose);
        basePoseValid = g_cameraRuntime.ReadActivePose(basePose);
        mouseHandsCorrectionReady = ApplyMouseAngularPreview(
            clockSample, currentSerial, mouseHandsCorrection, mousePreview);
        finalPoseValid = g_cameraRuntime.ReadActivePose(finalPose);
    }
    if (g_motionTraceEnabled) {
        std::uint32_t traceFlags = 0;
        if (presentationPoll.polled) traceFlags |= 1U << 0U;
        if (cameraApplied) traceFlags |= 1U << 1U;
        if (authoritativePoseValid) traceFlags |= 1U << 2U;
        if (basePoseValid) traceFlags |= 1U << 3U;
        if (finalPoseValid) traceFlags |= 1U << 4U;
        if (mousePreview.historyValid) traceFlags |= 1U << 5U;
        if (mousePreview.pendingScaleValid) traceFlags |= 1U << 6U;
        if (mousePreview.applied) traceFlags |= 1U << 7U;
        if (phaseValid) traceFlags |= 1U << 8U;
        motion_trace::Record({
            presentationCounter.QuadPart,
            currentSerial,
            clockSample.olderSerial,
            clockSample.newerSerial,
            rawInterpolationAlpha,
            presentationDelayTicks,
            clockSample.alpha,
            authoritativePose,
            basePose,
            finalPose,
            presentationPoll.x,
            presentationPoll.y,
            deferredSinceRenderX,
            deferredSinceRenderY,
            mousePreview.pendingX,
            mousePreview.pendingY,
            mousePreview.historyPitch,
            mousePreview.historyYaw,
            mousePreview.pendingPitch,
            mousePreview.pendingYaw,
            traceFlags,
        });
    }
    if (g_handsInterpolationEnabled) {
        if (!g_handsRuntime.SetPresentationCorrection(
                mouseHandsCorrectionReady ? &mouseHandsCorrection : nullptr)) {
            g_mouseHandsCorrectionFailed.fetch_add(1, std::memory_order_relaxed);
            mouseHandsCorrectionReady = false;
        }
        const hands::FrameResult handsResult = g_handsRuntime.BeginRender(clockSample);
        RecordHandsResult(handsResult);
        if (mouseHandsCorrectionReady &&
            handsResult == hands::FrameResult::renderReady) {
            g_mouseHandsCorrectionApplied.fetch_add(1, std::memory_order_relaxed);
        }
        RecordHandAttachments(g_handsRuntime.LastAttachmentBatch());
    }
    if (g_worldInterpolationEnabled) {
        const world::BatchResult worldResult = g_worldRuntime.BeginRender(clockSample);
        RecordWorldBatch(worldResult);
    }
    if (g_handsPoseInterpolationEnabled) {
        const pose::UpdateResult result = g_poseRuntime.BeginRender(
            clockSample, g_tickSerial.load(std::memory_order_relaxed), frameTime);
        RecordPoseUpdate(result);
        if (result == pose::UpdateResult::invalid && !g_poseInvalidDiagnosticLogged) {
            std::size_t boneIndex = 0;
            hands::Matrix matrix{};
            if (g_poseRuntime.TakeInvalidMatrixDiagnostic(boneIndex, matrix)) {
                std::wstring values;
                wchar_t number[32]{};
                for (std::size_t index = 0; index < matrix.values.size(); ++index) {
                    _snwprintf_s(number, std::size(number), _TRUNCATE,
                                 index == 0 ? L"%.6g" : L",%.6g",
                                 static_cast<double>(matrix.values[index]));
                    values += number;
                }
                log::Write(log::Level::warning,
                           L"Hands pose diagnostic: first invalid affine bone index=" +
                               std::to_wstring(boneIndex) + L" matrix=[" + values + L"]");
                g_poseInvalidDiagnosticLogged = true;
            }
        }
    }
    if (g_worldPoseInterpolationEnabled) {
        RecordEnemyPoseBatch(g_enemyPoseRuntime.BeginRender(
            clockSample, g_tickSerial.load(std::memory_order_relaxed), frameTime));
    }
    g_particlePresentationActive =
        g_particleInterpolationEnabled &&
        (clockSample.kind == clock::SampleKind::interpolated ||
         clockSample.kind == clock::SampleKind::clampedToOldest ||
         clockSample.kind == clock::SampleKind::clampedToNewest) &&
        std::isfinite(clockSample.alpha) && clockSample.alpha >= 0.0 &&
        clockSample.alpha <= 1.0;
    g_particlePresentationAlpha =
        g_particlePresentationActive ? clockSample.alpha : 0.0;
    g_originalSceneRender(self, frameTime, flags);
    g_particlePresentationActive = false;
    if (g_worldPoseInterpolationEnabled) {
        RecordEnemyPoseBatch(g_enemyPoseRuntime.EndRender());
    }
    if (g_handsPoseInterpolationEnabled && !g_poseRuntime.EndRender()) {
        g_poseRestoreFailures.fetch_add(1, std::memory_order_relaxed);
    }
    if (g_worldInterpolationEnabled) {
        g_worldModelRestoreFailed.fetch_add(g_worldRuntime.EndRender(), std::memory_order_relaxed);
    }
    if (g_handsInterpolationEnabled) {
        g_handAttachmentRestoreFailures.fetch_add(g_handsRuntime.EndRender(),
                                                  std::memory_order_relaxed);
    }
    if (cameraApplied && !g_cameraRuntime.Restore()) {
        g_cameraRestoreFailures.fetch_add(1, std::memory_order_relaxed);
        g_lastCameraResult.store(camera::FrameResult::applyFailed, std::memory_order_relaxed);
    }
    g_sceneRenders.fetch_add(1, std::memory_order_relaxed);
}

std::wstring Rate(const std::uint32_t count, const double seconds) {
    wchar_t buffer[32]{};
    _snwprintf_s(buffer, std::size(buffer), _TRUNCATE, L"%.3f",
                 seconds > 0.0 ? static_cast<double>(count) / seconds : 0.0);
    return buffer;
}

std::wstring Decimal(const double value) {
    wchar_t buffer[32]{};
    _snwprintf_s(buffer, std::size(buffer), _TRUNCATE, L"%.3f", value);
    return buffer;
}

DWORD WINAPI ReporterThread(void*) {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER previousTime{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previousTime);

    std::uint32_t previousTicks = 0;
    std::uint32_t previousDraws = 0;
    std::uint32_t previousPostRenders = 0;
    std::uint32_t previousSceneRenders = 0;
    std::uint32_t previousClockInterpolated = 0;
    std::uint32_t previousClockClampedOldest = 0;
    std::uint32_t previousClockClampedNewest = 0;
    std::uint32_t previousClockUnavailable = 0;
    std::uint32_t previousClockResets = 0;
    std::uint32_t previousClockPhaseRebases = 0;
    std::uint32_t previousCameraApplied = 0;
    std::uint32_t previousCameraCaptured = 0;
    std::uint32_t previousCameraNoScene = 0;
    std::uint32_t previousCameraNoView = 0;
    std::uint32_t previousCameraIdentityChanges = 0;
    std::uint32_t previousCameraGenerationResets = 0;
    std::uint32_t previousCameraNonMonotonicResets = 0;
    std::uint32_t previousCameraSameTickMutations = 0;
    std::uint32_t previousCameraDiscontinuityResets = 0;
    std::uint32_t previousCameraClockWaits = 0;
    std::uint32_t previousCameraSnapshotMisses = 0;
    std::uint32_t previousCameraInvalid = 0;
    std::uint32_t previousCameraApplyFailures = 0;
    std::uint32_t previousCameraRestoreFailures = 0;
    const DWORD waitMilliseconds = g_reportIntervalSeconds * 1000U;

    while (WaitForSingleObject(g_stopEvent, waitMilliseconds) == WAIT_TIMEOUT) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const double seconds = static_cast<double>(now.QuadPart - previousTime.QuadPart) /
                               static_cast<double>(frequency.QuadPart);

        const std::uint32_t ticks = g_completedTicks.load(std::memory_order_relaxed);
        const std::uint32_t draws = g_onDrawMessages.load(std::memory_order_relaxed);
        const std::uint32_t postRenders = g_postRenderMessages.load(std::memory_order_relaxed);
        const std::uint32_t sceneRenders = g_sceneRenders.load(std::memory_order_relaxed);
        const std::uint32_t clockInterpolated = g_clockInterpolated.load(std::memory_order_relaxed);
        const std::uint32_t clockClampedOldest =
            g_clockClampedOldest.load(std::memory_order_relaxed);
        const std::uint32_t clockClampedNewest =
            g_clockClampedNewest.load(std::memory_order_relaxed);
        const std::uint32_t clockUnavailable = g_clockUnavailable.load(std::memory_order_relaxed);
        const std::uint32_t clockResets = g_clockResets.load(std::memory_order_relaxed);
        const std::uint32_t clockPhaseRebases = g_clockPhaseRebases.load(std::memory_order_relaxed);
        const std::uint32_t cameraApplied = g_cameraApplied.load(std::memory_order_relaxed);
        const std::uint32_t cameraCaptured = g_cameraCaptured.load(std::memory_order_relaxed);
        const std::uint32_t cameraNoScene = g_cameraNoScene.load(std::memory_order_relaxed);
        const std::uint32_t cameraNoView = g_cameraNoView.load(std::memory_order_relaxed);
        const std::uint32_t cameraIdentityChanges =
            g_cameraIdentityChanges.load(std::memory_order_relaxed);
        const std::uint32_t cameraGenerationResets =
            g_cameraGenerationResets.load(std::memory_order_relaxed);
        const std::uint32_t cameraNonMonotonicResets =
            g_cameraNonMonotonicResets.load(std::memory_order_relaxed);
        const std::uint32_t cameraSameTickMutations =
            g_cameraSameTickMutations.load(std::memory_order_relaxed);
        const std::uint32_t cameraDiscontinuityResets =
            g_cameraDiscontinuityResets.load(std::memory_order_relaxed);
        const std::uint32_t cameraClockWaits = g_cameraClockWaits.load(std::memory_order_relaxed);
        const std::uint32_t cameraSnapshotMisses =
            g_cameraSnapshotMisses.load(std::memory_order_relaxed);
        const std::uint32_t cameraInvalid = g_cameraInvalid.load(std::memory_order_relaxed);
        const std::uint32_t cameraApplyFailures =
            g_cameraApplyFailures.load(std::memory_order_relaxed);
        const std::uint32_t cameraRestoreFailures =
            g_cameraRestoreFailures.load(std::memory_order_relaxed);
        const std::uint32_t tickDelta = ticks - previousTicks;
        const std::uint32_t drawDelta = draws - previousDraws;
        const std::uint32_t postRenderDelta = postRenders - previousPostRenders;
        const std::uint32_t sceneDelta = sceneRenders - previousSceneRenders;
        const cadence::Snapshot frameCadence = g_frameCadence.TakeSnapshot();

        log::Write(
            log::Level::info,
            L"Telemetry interval=" + Decimal(seconds) + L"s RenderUnlock=" +
                RenderUnlockStateName(g_renderUnlockState.load(std::memory_order_acquire)) +
                L" ticks=" + std::to_wstring(tickDelta) + L" (" + Rate(tickDelta, seconds) +
                L" Hz) OnDraw=" + std::to_wstring(drawDelta) + L" (" + Rate(drawDelta, seconds) +
                L" Hz) SceneRender=" + std::to_wstring(sceneDelta) + L" (" +
                Rate(sceneDelta, seconds) + L" Hz) OnPostRender=" +
                std::to_wstring(postRenderDelta) + L" (" + Rate(postRenderDelta, seconds) +
                L" Hz) FrameCadence(samples=" + std::to_wstring(frameCadence.samples) +
                L", meanMs=" + Decimal(frameCadence.meanMilliseconds) +
                L", p50Ms=" + Decimal(frameCadence.p50Milliseconds) +
                L", p95Ms=" + Decimal(frameCadence.p95Milliseconds) +
                L", p99Ms=" + Decimal(frameCadence.p99Milliseconds) +
                L", maxMs=" + Decimal(frameCadence.maxMilliseconds) +
                L", longGap=" + std::to_wstring(frameCadence.longGaps) +
                L", overflow=" + std::to_wstring(frameCadence.overflow) +
                L", invalid=" + std::to_wstring(frameCadence.invalid) +
                L") Clock(interp=" +
                std::to_wstring(clockInterpolated - previousClockInterpolated) + L", clampOld=" +
                std::to_wstring(clockClampedOldest - previousClockClampedOldest) + L", clampNew=" +
                std::to_wstring(clockClampedNewest - previousClockClampedNewest) +
                L", unavailable=" + std::to_wstring(clockUnavailable - previousClockUnavailable) +
                L", resets=" + std::to_wstring(clockResets - previousClockResets) +
                L", phaseRebase=" + std::to_wstring(clockPhaseRebases - previousClockPhaseRebases) +
                L", alpha=" +
                Decimal(
                    static_cast<double>(g_latestAlphaMillionths.load(std::memory_order_relaxed)) /
                    1'000'000.0) +
                L", lastReset=" +
                clock::ResetReasonName(g_lastClockReset.load(std::memory_order_relaxed)) +
                L") Camera(captured=" + std::to_wstring(cameraCaptured - previousCameraCaptured) +
                L", applied=" + std::to_wstring(cameraApplied - previousCameraApplied) +
                L", noScene=" + std::to_wstring(cameraNoScene - previousCameraNoScene) +
                L", noView=" + std::to_wstring(cameraNoView - previousCameraNoView) +
                L", viewFail(noListener=" +
                std::to_wstring(g_cameraNoListener.exchange(0, std::memory_order_relaxed)) +
                L", hidden=" +
                std::to_wstring(g_cameraListenerHidden.exchange(0, std::memory_order_relaxed)) +
                L", noCamera=" +
                std::to_wstring(
                    g_cameraListenerMissingCamera.exchange(0, std::memory_order_relaxed)) +
                L", noWorld=" +
                std::to_wstring(
                    g_cameraListenerMissingWorld.exchange(0, std::memory_order_relaxed)) +
                L", invalid=" +
                std::to_wstring(g_cameraInvalidViewport.exchange(0, std::memory_order_relaxed)) +
                L")" + L", identity=" +
                std::to_wstring(cameraIdentityChanges - previousCameraIdentityChanges) +
                L", genReset=" +
                std::to_wstring(cameraGenerationResets - previousCameraGenerationResets) +
                L", nonMono=" +
                std::to_wstring(cameraNonMonotonicResets - previousCameraNonMonotonicResets) +
                L", sameTick=" +
                std::to_wstring(cameraSameTickMutations - previousCameraSameTickMutations) +
                L", jump=" +
                std::to_wstring(cameraDiscontinuityResets - previousCameraDiscontinuityResets) +
                L", clockWait=" + std::to_wstring(cameraClockWaits - previousCameraClockWaits) +
                L", pairMiss=" +
                std::to_wstring(cameraSnapshotMisses - previousCameraSnapshotMisses) +
                L", invalid=" + std::to_wstring(cameraInvalid - previousCameraInvalid) +
                L", applyFail=" +
                std::to_wstring(cameraApplyFailures - previousCameraApplyFailures) +
                L", restoreFail=" +
                std::to_wstring(cameraRestoreFailures - previousCameraRestoreFailures) +
                L", last=" +
                camera::FrameResultName(g_lastCameraResult.load(std::memory_order_relaxed)) +
                L") MouseAudit(updates=" +
                std::to_wstring(g_mouseUpdates.exchange(0, std::memory_order_relaxed)) +
                L", acquiredNonZero=" +
                std::to_wstring(
                    g_mouseAcquiredNonZero.exchange(0, std::memory_order_relaxed)) +
                L", reads=" +
                std::to_wstring(g_mouseReads.exchange(0, std::memory_order_relaxed)) +
                L", gameplayReads=" +
                std::to_wstring(g_mouseGameplayReads.exchange(0, std::memory_order_relaxed)) +
                L", retiredNonZero=" +
                std::to_wstring(
                    g_mouseRetiredNonZero.exchange(0, std::memory_order_relaxed)) +
                L", acquiredXY=" +
                std::to_wstring(g_mouseAcquiredX.exchange(0, std::memory_order_relaxed)) + L"/" +
                std::to_wstring(g_mouseAcquiredY.exchange(0, std::memory_order_relaxed)) +
                L", retiredXY=" +
                std::to_wstring(g_mouseRetiredX.exchange(0, std::memory_order_relaxed)) + L"/" +
                std::to_wstring(g_mouseRetiredY.exchange(0, std::memory_order_relaxed)) +
                L", overwrite=" +
                std::to_wstring(
                    g_mouseUnconsumedOverwrites.exchange(0, std::memory_order_relaxed)) +
                L", playerCalls=" +
                std::to_wstring(g_mousePlayerCalls.exchange(0, std::memory_order_relaxed)) +
                L", earlyExit=" +
                std::to_wstring(
                    g_mousePlayerEarlyExits.exchange(0, std::memory_order_relaxed)) +
                L", multiRead=" +
                std::to_wstring(
                    g_mousePlayerMultipleReads.exchange(0, std::memory_order_relaxed)) +
                L", smoothOn=" +
                std::to_wstring(g_mouseSmoothOn.exchange(0, std::memory_order_relaxed)) +
                L", smoothOff=" +
                std::to_wstring(g_mouseSmoothOff.exchange(0, std::memory_order_relaxed)) +
                L", invalid=" +
                std::to_wstring(g_mouseStateInvalid.exchange(0, std::memory_order_relaxed)) +
                L", presentPolls=" +
                std::to_wstring(
                    g_mousePresentationPolls.exchange(0, std::memory_order_relaxed)) +
                L", presentNonZero=" +
                std::to_wstring(
                    g_mousePresentationNonZero.exchange(0, std::memory_order_relaxed)) +
                L", presentXY=" +
                std::to_wstring(
                    g_mousePresentationX.exchange(0, std::memory_order_relaxed)) + L"/" +
                std::to_wstring(
                    g_mousePresentationY.exchange(0, std::memory_order_relaxed)) +
                L", pendingDeliveries=" +
                std::to_wstring(
                    g_mousePendingDeliveries.exchange(0, std::memory_order_relaxed)) +
                L", deliveredXY=" +
                std::to_wstring(
                    g_mousePendingDeliveredX.exchange(0, std::memory_order_relaxed)) + L"/" +
                std::to_wstring(
                    g_mousePendingDeliveredY.exchange(0, std::memory_order_relaxed)) +
                L", tickDeferred=" +
                std::to_wstring(
                    g_mouseTickDeferredPolls.exchange(0, std::memory_order_relaxed)) +
                L", tickDeferredNonZero=" +
                std::to_wstring(
                    g_mouseTickDeferredNonZero.exchange(0, std::memory_order_relaxed)) +
                L", deferredXY=" +
                std::to_wstring(
                    g_mouseTickDeferredX.exchange(0, std::memory_order_relaxed)) + L"/" +
                std::to_wstring(
                    g_mouseTickDeferredY.exchange(0, std::memory_order_relaxed)) +
                L", threadMismatch=" +
                std::to_wstring(g_mousePresentationThreadMismatches.exchange(
                    0, std::memory_order_relaxed)) +
                L", mergeOverflow=" +
                std::to_wstring(
                    g_mouseMergeOverflows.exchange(0, std::memory_order_relaxed)) +
                L", rawTap=" +
                std::wstring(g_rawMouseTapInstalled.load(std::memory_order_relaxed)
                                 ? L"installed" : L"fallback") +
                L", rawActive=" +
                std::wstring(g_rawMouseActive.load(std::memory_order_relaxed)
                                 ? L"yes" : L"no") +
                L", rawPackets=" +
                std::to_wstring(g_rawMousePackets.exchange(0, std::memory_order_relaxed)) +
                L", rawNonZero=" +
                std::to_wstring(
                    g_rawMouseNonZeroPackets.exchange(0, std::memory_order_relaxed)) +
                L", rawXY=" +
                std::to_wstring(g_rawMousePacketX.exchange(0, std::memory_order_relaxed)) +
                L"/" +
                std::to_wstring(g_rawMousePacketY.exchange(0, std::memory_order_relaxed)) +
                L", rawTickCompare=" +
                std::to_wstring(
                    g_rawMouseTickComparisons.exchange(0, std::memory_order_relaxed)) +
                L", rawTickMismatch=" +
                std::to_wstring(
                    g_rawMouseTickMismatches.exchange(0, std::memory_order_relaxed)) +
                L", rawInstallFail=" +
                std::to_wstring(
                    g_rawMouseInstallFailures.exchange(0, std::memory_order_relaxed)) +
                L", rawDirectDrain=" +
                std::to_wstring(
                    g_rawMouseDirectDrainCalls.exchange(0, std::memory_order_relaxed)) +
                L"/" +
                std::to_wstring(g_rawMouseDirectDrainMessages.exchange(
                    0, std::memory_order_relaxed)) +
                L", guiStock=" +
                std::to_wstring(
                    g_guiMouseStockCalls.exchange(0, std::memory_order_relaxed)) +
                L", guiPresent=" +
                std::to_wstring(
                    g_guiMousePresentationCalls.exchange(0, std::memory_order_relaxed)) +
                L", guiMoves=" +
                std::to_wstring(
                    g_guiMousePresentationMoves.exchange(0, std::memory_order_relaxed)) +
                L", guiModeSamples=" +
                std::to_wstring(
                    g_guiMouseRelativeSamples.exchange(0, std::memory_order_relaxed)) + L"/" +
                std::to_wstring(
                    g_guiMouseAbsoluteSamples.exchange(0, std::memory_order_relaxed)) +
                L", guiXY=" +
                std::to_wstring(
                    g_guiMousePresentationX.exchange(0, std::memory_order_relaxed)) + L"/" +
                std::to_wstring(
                    g_guiMousePresentationY.exchange(0, std::memory_order_relaxed)) +
                L", guiFixedCorrection=" +
                std::to_wstring(
                    g_guiMouseFixedCorrections.exchange(0, std::memory_order_relaxed)) +
                L", guiStale=" +
                std::to_wstring(
                    g_guiMouseStaleSkips.exchange(0, std::memory_order_relaxed)) +
                L", guiInvalid=" +
                std::to_wstring(
                    g_guiMouseInvalidSkips.exchange(0, std::memory_order_relaxed)) +
                L", guiThreadMismatch=" +
                std::to_wstring(g_guiMouseThreadMismatches.exchange(
                    0, std::memory_order_relaxed)) +
                L", commandCaptured=" +
                std::to_wstring(
                    g_mouseCommandCaptured.exchange(0, std::memory_order_relaxed)) +
                L", commandMissing=" +
                std::to_wstring(
                    g_mouseCommandMissing.exchange(0, std::memory_order_relaxed)) +
                L", commandMultiAdd=" +
                std::to_wstring(
                    g_mouseCommandMultiAdd.exchange(0, std::memory_order_relaxed)) +
                L", scaleAccept=" +
                std::to_wstring(
                    g_mouseScaleAccepted.exchange(0, std::memory_order_relaxed)) +
                L", scaleReject=" +
                std::to_wstring(
                    g_mouseScaleRejected.exchange(0, std::memory_order_relaxed)) +
                L", scaleFrozen=" +
                std::to_wstring(
                    g_mouseScaleFrozen.exchange(0, std::memory_order_relaxed)) +
                L", commandShape=" +
                std::to_wstring(
                    g_mouseCommandDirect.exchange(0, std::memory_order_relaxed)) + L"/" +
                std::to_wstring(
                    g_mouseCommandTransformed.exchange(0, std::memory_order_relaxed)) + L"/" +
                std::to_wstring(
                    g_mouseCommandNoSignal.exchange(0, std::memory_order_relaxed)) + L"/" +
                std::to_wstring(
                    g_mouseCommandShapeInvalid.exchange(0, std::memory_order_relaxed)) +
                L", fallbackTransitions=" +
                std::to_wstring(
                    g_mouseFallbackEntered.exchange(0, std::memory_order_relaxed)) + L"/" +
                std::to_wstring(
                    g_mouseFallbackExited.exchange(0, std::memory_order_relaxed)) +
                L", playerDequantized=" +
                std::to_wstring(
                    g_mousePlayerDequantized.exchange(0, std::memory_order_relaxed)) +
                L", playerDequantizeReject=" +
                std::to_wstring(g_mousePlayerDequantizeRejected.exchange(
                    0, std::memory_order_relaxed)) +
                L", previewApplied=" +
                std::to_wstring(
                    g_mousePreviewApplied.exchange(0, std::memory_order_relaxed)) +
                L", previewNoScale=" +
                std::to_wstring(
                    g_mousePreviewNoScale.exchange(0, std::memory_order_relaxed)) +
                L", previewNoCommand=" +
                std::to_wstring(
                    g_mousePreviewNoCommand.exchange(0, std::memory_order_relaxed)) +
                L", previewPendingSuppressed=" +
                std::to_wstring(g_mousePreviewPendingSuppressed.exchange(
                    0, std::memory_order_relaxed)) +
                L", previewTransformedBypass=" +
                std::to_wstring(g_mousePreviewTransformedBypass.exchange(
                    0, std::memory_order_relaxed)) +
                L", previewApplyFail=" +
                std::to_wstring(
                    g_mousePreviewApplyFailed.exchange(0, std::memory_order_relaxed)) +
                L", previewYawRebased=" +
                std::to_wstring(
                    g_mousePreviewYawRebased.exchange(0, std::memory_order_relaxed)) +
                L", previewPendingXY=" +
                std::to_wstring(
                    g_mousePreviewPendingX.exchange(0, std::memory_order_relaxed)) + L"/" +
                std::to_wstring(
                    g_mousePreviewPendingY.exchange(0, std::memory_order_relaxed)) +
                L", handsCorrectionBuilt=" +
                std::to_wstring(
                    g_mouseHandsCorrectionBuilt.exchange(0, std::memory_order_relaxed)) +
                L", handsCorrectionApplied=" +
                std::to_wstring(
                    g_mouseHandsCorrectionApplied.exchange(0, std::memory_order_relaxed)) +
                L", handsCorrectionFail=" +
                std::to_wstring(
                    g_mouseHandsCorrectionFailed.exchange(0, std::memory_order_relaxed)) +
                L") Hands(staged=" +
                std::to_wstring(g_handsStaged.exchange(0, std::memory_order_relaxed)) +
                L", captured=" +
                std::to_wstring(g_handsCaptured.exchange(0, std::memory_order_relaxed)) +
                L", ready=" +
                std::to_wstring(g_handsRenderReady.exchange(0, std::memory_order_relaxed)) +
                L", handModels=" +
                std::to_wstring(g_handsModelOverrides.exchange(0, std::memory_order_relaxed)) +
                L", objectModels=" +
                std::to_wstring(g_handObjectModelOverrides.exchange(0, std::memory_order_relaxed)) +
                L", objectHistory=" +
                std::to_wstring(g_handObjectHistoryOverrides.exchange(
                    0, std::memory_order_relaxed)) +
                L", unavailable=" +
                std::to_wstring(g_handsUnavailable.exchange(0, std::memory_order_relaxed)) +
                L", identity=" +
                std::to_wstring(g_handsIdentityChanges.exchange(0, std::memory_order_relaxed)) +
                L", resets=" +
                std::to_wstring(g_handsResets.exchange(0, std::memory_order_relaxed)) +
                L", clockWait=" +
                std::to_wstring(g_handsClockWaits.exchange(0, std::memory_order_relaxed)) +
                L", pairMiss=" +
                std::to_wstring(g_handsSnapshotMisses.exchange(0, std::memory_order_relaxed)) +
                L", invalid=" +
                std::to_wstring(g_handsInvalid.exchange(0, std::memory_order_relaxed)) +
                L", applyFail=" +
                std::to_wstring(g_handsApplyFailures.exchange(0, std::memory_order_relaxed)) +
                L", slotFull=" +
                std::to_wstring(g_handsSlotExhausted.exchange(0, std::memory_order_relaxed)) +
                L", attachments=" +
                std::to_wstring(g_handAttachmentApplied.exchange(0,
                                                                  std::memory_order_relaxed)) +
                L"/" +
                std::to_wstring(g_handAttachmentCandidates.exchange(
                    0, std::memory_order_relaxed)) +
                L", attachmentParentMiss=" +
                std::to_wstring(g_handAttachmentParentMismatch.exchange(
                    0, std::memory_order_relaxed)) +
                L", attachmentInvalid=" +
                std::to_wstring(g_handAttachmentInvalid.exchange(
                    0, std::memory_order_relaxed)) +
                L", attachmentFull=" +
                std::to_wstring(g_handAttachmentCapacity.exchange(
                    0, std::memory_order_relaxed)) +
                L", attachmentRestoreFail=" +
                std::to_wstring(g_handAttachmentRestoreFailures.exchange(
                    0, std::memory_order_relaxed)) +
                L", last=" +
                hands::FrameResultName(g_lastHandsResult.load(std::memory_order_relaxed)) +
                L") World(props=" +
                std::to_wstring(g_worldPropsStaged.exchange(0, std::memory_order_relaxed)) +
                L", grunts=" +
                std::to_wstring(g_worldGruntsStaged.exchange(0, std::memory_order_relaxed)) +
                L", captured=" +
                std::to_wstring(g_worldCaptured.exchange(0, std::memory_order_relaxed)) +
                L", sleeping=" +
                std::to_wstring(g_worldSleeping.exchange(0, std::memory_order_relaxed)) +
                L", sleepCached=" +
                std::to_wstring(g_worldCachedSleeping.exchange(0, std::memory_order_relaxed)) +
                L", moved=" +
                std::to_wstring(g_worldMovedCallbacks.exchange(0, std::memory_order_relaxed)) +
                L", movedMatched=" +
                std::to_wstring(g_worldMovedMatched.exchange(0, std::memory_order_relaxed)) +
                L", movedStandalone=" +
                std::to_wstring(
                    g_worldMovedStandalone.exchange(0, std::memory_order_relaxed)) +
                L", movedOverflow=" +
                std::to_wstring(g_worldMovedOverflow.exchange(0, std::memory_order_relaxed)) +
                L", ready=" + std::to_wstring(g_worldReady.exchange(0, std::memory_order_relaxed)) +
                L", propModels=" +
                std::to_wstring(g_worldPropModels.exchange(0, std::memory_order_relaxed)) +
                L", propParentModels=" +
                std::to_wstring(g_worldPropParentModels.exchange(0, std::memory_order_relaxed)) +
                L", propNullModels=" +
                std::to_wstring(g_worldPropNullModels.exchange(0, std::memory_order_relaxed)) +
                L", propDirectModels=" +
                std::to_wstring(
                    g_worldPropDirectParentModels.exchange(0, std::memory_order_relaxed)) +
                L", propHistoryModels=" +
                std::to_wstring(
                    g_worldPropModelHistoryModels.exchange(0, std::memory_order_relaxed)) +
                L", multiOwners=" +
                std::to_wstring(g_worldMultiBodyOwners.exchange(0, std::memory_order_relaxed)) +
                L", modelCandidates=" +
                std::to_wstring(g_worldModelCandidates.exchange(0, std::memory_order_relaxed)) +
                L", modelCaptured=" +
                std::to_wstring(g_worldModelCaptured.exchange(0, std::memory_order_relaxed)) +
                L", modelInvalid=" +
                std::to_wstring(g_worldModelInvalid.exchange(0, std::memory_order_relaxed)) +
                L", modelCapacity=" +
                std::to_wstring(g_worldModelCapacity.exchange(0, std::memory_order_relaxed)) +
                L", modelReady=" +
                std::to_wstring(g_worldModelReady.exchange(0, std::memory_order_relaxed)) +
                L", modelMovingReady=" +
                std::to_wstring(g_worldModelMovingReady.exchange(0, std::memory_order_relaxed)) +
                L", modelCacheApplied=" +
                std::to_wstring(g_worldModelCacheApplied.exchange(0, std::memory_order_relaxed)) +
                L", modelCacheInvalid=" +
                std::to_wstring(g_worldModelCacheInvalid.exchange(0, std::memory_order_relaxed)) +
                L", modelRestoreFail=" +
                std::to_wstring(g_worldModelRestoreFailed.exchange(0, std::memory_order_relaxed)) +
                L", modelHistSeen=" +
                std::to_wstring(g_worldModelHistorySeen.exchange(0, std::memory_order_relaxed)) +
                L", modelHistBody=" +
                std::to_wstring(
                    g_worldModelHistoryBodyPriority.exchange(0, std::memory_order_relaxed)) +
                L", modelHistOwnerInvalid=" +
                std::to_wstring(
                    g_worldModelHistoryOwnerInvalid.exchange(0, std::memory_order_relaxed)) +
                L", modelHistOwnerMiss=" +
                std::to_wstring(
                    g_worldModelHistoryOwnerMissing.exchange(0, std::memory_order_relaxed)) +
                L", modelHistNotReady=" +
                std::to_wstring(
                    g_worldModelHistoryOwnerNotReady.exchange(0, std::memory_order_relaxed)) +
                L", modelHistStale=" +
                std::to_wstring(g_worldModelHistoryStale.exchange(0, std::memory_order_relaxed)) +
                L", modelHistClock=" +
                std::to_wstring(
                    g_worldModelHistoryClockWait.exchange(0, std::memory_order_relaxed)) +
                L", modelHistGeneration=" +
                std::to_wstring(
                    g_worldModelHistoryGeneration.exchange(0, std::memory_order_relaxed)) +
                L", modelHistPair=" +
                std::to_wstring(
                    g_worldModelHistoryPairMiss.exchange(0, std::memory_order_relaxed)) +
                L", modelHistInvalid=" +
                std::to_wstring(g_worldModelHistoryInvalid.exchange(0, std::memory_order_relaxed)) +
                L", modelHistMovingSame=" +
                std::to_wstring(
                    g_worldModelHistoryMovingUnchanged.exchange(0, std::memory_order_relaxed)) +
                L", modelHistMovingChanged=" +
                std::to_wstring(
                    g_worldModelHistoryMovingChanged.exchange(0, std::memory_order_relaxed)) +
                L", gruntModels=" +
                std::to_wstring(g_worldGruntModels.exchange(0, std::memory_order_relaxed)) +
                L", standaloneModels=" +
                std::to_wstring(g_worldStandaloneModels.exchange(0, std::memory_order_relaxed)) +
                L", standaloneNullModels=" +
                std::to_wstring(
                    g_worldStandaloneNullModels.exchange(0, std::memory_order_relaxed)) +
                L", resets=" +
                std::to_wstring(g_worldResets.exchange(0, std::memory_order_relaxed)) +
                L", pairMiss=" +
                std::to_wstring(g_worldPairMisses.exchange(0, std::memory_order_relaxed)) +
                L", invalid=" +
                std::to_wstring(g_worldInvalid.exchange(0, std::memory_order_relaxed)) +
                L", applyFail=" +
                std::to_wstring(g_worldApplyFailures.exchange(0, std::memory_order_relaxed)) +
                L", parentMiss=" +
                std::to_wstring(g_worldParentMisses.exchange(0, std::memory_order_relaxed)) +
                L", capacity=" +
                std::to_wstring(g_worldCapacityExhausted.exchange(0, std::memory_order_relaxed)) +
                L", slotFull=" +
                std::to_wstring(g_worldModelSlotsExhausted.exchange(0, std::memory_order_relaxed)) +
                L") Particles(calls=" +
                std::to_wstring(g_particleCalls.exchange(0, std::memory_order_relaxed)) +
                L", emitters=" +
                std::to_wstring(
                    g_particleEmittersApplied.exchange(0, std::memory_order_relaxed)) +
                L", positions=" +
                std::to_wstring(
                    g_particlePositionsApplied.exchange(0, std::memory_order_relaxed)) +
                L", inactive=" +
                std::to_wstring(g_particleInactive.exchange(0, std::memory_order_relaxed)) +
                L", invalid=" +
                std::to_wstring(g_particleInvalid.exchange(0, std::memory_order_relaxed)) +
                L", capacity=" +
                std::to_wstring(g_particleCapacity.exchange(0, std::memory_order_relaxed)) +
                L", restoreFail=" +
                std::to_wstring(
                    g_particleRestoreFailures.exchange(0, std::memory_order_relaxed)) +
                L") ImageTrail(calls=" +
                std::to_wstring(g_imageTrailCalls.exchange(0, std::memory_order_relaxed)) +
                L", adjusted=" +
                std::to_wstring(g_imageTrailAdjusted.exchange(0, std::memory_order_relaxed)) +
                L", unchanged=" +
                std::to_wstring(g_imageTrailUnchanged.exchange(0, std::memory_order_relaxed)) +
                L", clear=" +
                std::to_wstring(g_imageTrailClears.exchange(0, std::memory_order_relaxed)) +
                L", invalid=" +
                std::to_wstring(g_imageTrailInvalid.exchange(0, std::memory_order_relaxed)) +
                L", nested=" +
                std::to_wstring(g_imageTrailNested.exchange(0, std::memory_order_relaxed)) +
                L", restoreFail=" +
                std::to_wstring(
                    g_imageTrailRestoreFailures.exchange(0, std::memory_order_relaxed)) +
                L") Pose(staged=" +
                std::to_wstring(g_poseStaged.exchange(0, std::memory_order_relaxed)) +
                L", applied=" +
                std::to_wstring(g_poseApplied.exchange(0, std::memory_order_relaxed)) +
                L", repeat=" +
                std::to_wstring(g_poseAlreadyApplied.exchange(0, std::memory_order_relaxed)) +
                L", unavailable=" +
                std::to_wstring(g_poseUnavailable.exchange(0, std::memory_order_relaxed)) +
                L", identity=" +
                std::to_wstring(g_poseIdentityChanges.exchange(0, std::memory_order_relaxed)) +
                L", clockWait=" +
                std::to_wstring(g_poseClockWaits.exchange(0, std::memory_order_relaxed)) +
                L", pairMiss=" +
                std::to_wstring(g_posePairMisses.exchange(0, std::memory_order_relaxed)) +
                L", invalid=" +
                std::to_wstring(g_poseInvalid.exchange(0, std::memory_order_relaxed)) +
                L", restoreFail=" +
                std::to_wstring(g_poseRestoreFailures.exchange(0, std::memory_order_relaxed)) +
                L") EnemyPose(enemyStaged=" +
                std::to_wstring(
                    g_enemyPoseEnemyStaged.exchange(0, std::memory_order_relaxed)) +
                L", ragdollStaged=" +
                std::to_wstring(
                    g_enemyPoseRagdollStaged.exchange(0, std::memory_order_relaxed)) +
                L", targets=" +
                std::to_wstring(g_enemyPoseTargets.exchange(0, std::memory_order_relaxed)) +
                L", visibilitySkip=" +
                std::to_wstring(
                    g_enemyPoseVisibilitySkipped.exchange(0, std::memory_order_relaxed)) +
                L", visibilityUnknown=" +
                std::to_wstring(
                    g_enemyPoseVisibilityUnavailable.exchange(0, std::memory_order_relaxed)) +
                L", captured=" +
                std::to_wstring(g_enemyPoseCaptured.exchange(0, std::memory_order_relaxed)) +
                L", applied=" +
                std::to_wstring(g_enemyPoseApplied.exchange(0, std::memory_order_relaxed)) +
                L", identity=" +
                std::to_wstring(g_enemyPoseIdentityChanges.exchange(0, std::memory_order_relaxed)) +
                L", genReset=" +
                std::to_wstring(
                    g_enemyPoseGenerationResets.exchange(0, std::memory_order_relaxed)) +
                L", nonMono=" +
                std::to_wstring(
                    g_enemyPoseNonMonotonicResets.exchange(0, std::memory_order_relaxed)) +
                L", sameTick=" +
                std::to_wstring(
                    g_enemyPoseSameTickMutations.exchange(0, std::memory_order_relaxed)) +
                L", boneCount=" +
                std::to_wstring(g_enemyPoseBoneCountResets.exchange(0, std::memory_order_relaxed)) +
                L", jump=" +
                std::to_wstring(
                    g_enemyPoseDiscontinuityResets.exchange(0, std::memory_order_relaxed)) +
                L", clockWait=" +
                std::to_wstring(g_enemyPoseClockWaits.exchange(0, std::memory_order_relaxed)) +
                L", pairMiss=" +
                std::to_wstring(g_enemyPosePairMisses.exchange(0, std::memory_order_relaxed)) +
                L", invalid=" +
                std::to_wstring(g_enemyPoseInvalid.exchange(0, std::memory_order_relaxed)) +
                L", unavailable=" +
                std::to_wstring(g_enemyPoseUnavailable.exchange(0, std::memory_order_relaxed)) +
                L", restoreFail=" +
                std::to_wstring(g_enemyPoseRestoreFailures.exchange(0, std::memory_order_relaxed)) +
                L", capacity=" +
                std::to_wstring(
                    g_enemyPoseCapacityExhausted.exchange(0, std::memory_order_relaxed)) +
                L")");

        if (g_motionTraceEnabled) {
            const motion_trace::FlushResult trace = motion_trace::Flush();
            log::Write(log::Level::info,
                       L"MotionTrace written=" + std::to_wstring(trace.written) +
                           L" dropped=" + std::to_wstring(trace.dropped));
        }

        previousTicks = ticks;
        previousDraws = draws;
        previousPostRenders = postRenders;
        previousSceneRenders = sceneRenders;
        previousClockInterpolated = clockInterpolated;
        previousClockClampedOldest = clockClampedOldest;
        previousClockClampedNewest = clockClampedNewest;
        previousClockUnavailable = clockUnavailable;
        previousClockResets = clockResets;
        previousClockPhaseRebases = clockPhaseRebases;
        previousCameraApplied = cameraApplied;
        previousCameraCaptured = cameraCaptured;
        previousCameraNoScene = cameraNoScene;
        previousCameraNoView = cameraNoView;
        previousCameraIdentityChanges = cameraIdentityChanges;
        previousCameraGenerationResets = cameraGenerationResets;
        previousCameraNonMonotonicResets = cameraNonMonotonicResets;
        previousCameraSameTickMutations = cameraSameTickMutations;
        previousCameraDiscontinuityResets = cameraDiscontinuityResets;
        previousCameraClockWaits = cameraClockWaits;
        previousCameraSnapshotMisses = cameraSnapshotMisses;
        previousCameraInvalid = cameraInvalid;
        previousCameraApplyFailures = cameraApplyFailures;
        previousCameraRestoreFailures = cameraRestoreFailures;
        previousTime = now;
    }
    return 0;
}

std::wstring HookError(const wchar_t* operation, const MH_STATUS status) {
    const char* statusText = MH_StatusToString(status);
    std::wstring wideStatus;
    if (statusText != nullptr) {
        while (*statusText != '\0') {
            wideStatus.push_back(static_cast<unsigned char>(*statusText));
            ++statusText;
        }
    }
    return std::wstring(operation) + L" failed: " + wideStatus;
}

void RollBackHooks(const target::Mapping& mapping) {
    MH_DisableHook(MH_ALL_HOOKS);
    if (mapping.engineRun != nullptr) {
        MH_RemoveHook(mapping.engineRun);
    }
    if (mapping.sceneRender != nullptr) {
        MH_RemoveHook(mapping.sceneRender);
    }
    if (mapping.updaterRunMessage != nullptr) {
        MH_RemoveHook(mapping.updaterRunMessage);
    }
    if (mapping.playerHandsPostUpdate != nullptr) {
        MH_RemoveHook(mapping.playerHandsPostUpdate);
    }
    if (mapping.luxPropOnUpdate != nullptr) {
        MH_RemoveHook(mapping.luxPropOnUpdate);
    }
    if (mapping.luxEnemyOnUpdate != nullptr) {
        MH_RemoveHook(mapping.luxEnemyOnUpdate);
    }
    if (mapping.newtonTransformCallback != nullptr) {
        MH_RemoveHook(mapping.newtonTransformCallback);
    }
    if (mapping.subMeshGetModelMatrix != nullptr) {
        MH_RemoveHook(mapping.subMeshGetModelMatrix);
    }
    if (mapping.particleEmitterUpdateGraphics != nullptr) {
        MH_RemoveHook(mapping.particleEmitterUpdateGraphics);
    }
    if (mapping.imageTrailRenderEffect != nullptr) {
        MH_RemoveHook(mapping.imageTrailRenderEffect);
    }
    if (mapping.mouseSdlUpdate != nullptr) {
        MH_RemoveHook(mapping.mouseSdlUpdate);
    }
    if (mapping.mouseSdlGetRelPosition != nullptr) {
        MH_RemoveHook(mapping.mouseSdlGetRelPosition);
    }
    if (mapping.guiSendMousePos != nullptr) {
        MH_RemoveHook(mapping.guiSendMousePos);
    }
    if (mapping.luxUpdateGamePlayerInput != nullptr) {
        MH_RemoveHook(mapping.luxUpdateGamePlayerInput);
    }
    if (mapping.luxPlayerAddYaw != nullptr) {
        MH_RemoveHook(mapping.luxPlayerAddYaw);
    }
    if (mapping.luxPlayerAddPitch != nullptr) {
        MH_RemoveHook(mapping.luxPlayerAddPitch);
    }
    if (mapping.cameraAddPitch != nullptr) {
        MH_RemoveHook(mapping.cameraAddPitch);
    }
    if (mapping.cameraAddYaw != nullptr) {
        MH_RemoveHook(mapping.cameraAddYaw);
    }
    MH_Uninitialize();
}

} // namespace

bool Initialize(const HMODULE executable, const bool telemetryEnabled,
                const bool renderUnlockEnabled, const bool cameraInterpolationEnabled,
                const bool handsInterpolationEnabled, const bool worldInterpolationEnabled,
                const bool handsPoseInterpolationEnabled, const bool worldPoseInterpolationEnabled,
                const bool ragdollPoseInterpolationEnabled,
                const bool particleInterpolationEnabled,
                const bool imageTrailTimingEnabled,
                const bool mouseAuditEnabled,
                const bool motionTraceEnabled, const std::wstring& motionTracePath,
                const unsigned int reportIntervalSeconds, std::wstring& error) {
    target::Mapping mapping{};
    if (!target::ResolveAndValidate(executable, mapping, error)) {
        return false;
    }
    if (mouseAuditEnabled) {
        HMODULE sdlModule = nullptr;
        const auto sdlAddress = reinterpret_cast<const wchar_t*>(
            mapping.sdlGetRelativeMouseState);
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                sdlAddress, &sdlModule)) {
            error = L"Could not identify the SDL2 module for GUI mouse-mode detection";
            return false;
        }
        g_sdlGetRelativeMouseMode = reinterpret_cast<SdlGetRelativeMouseMode>(
            GetProcAddress(sdlModule, "SDL_GetRelativeMouseMode"));
        if (g_sdlGetRelativeMouseMode == nullptr) {
            error = L"SDL_GetRelativeMouseMode is unavailable for GUI mouse-mode detection";
            return false;
        }
    }
    const bool timingHooksEnabled = telemetryEnabled || cameraInterpolationEnabled ||
                                    handsInterpolationEnabled || worldInterpolationEnabled ||
                                    handsPoseInterpolationEnabled || worldPoseInterpolationEnabled ||
                                    particleInterpolationEnabled || mouseAuditEnabled ||
                                    motionTraceEnabled;
    const bool interpolationClockEnabled = cameraInterpolationEnabled ||
                                           handsInterpolationEnabled ||
                                           worldInterpolationEnabled ||
                                           handsPoseInterpolationEnabled ||
                                           worldPoseInterpolationEnabled ||
                                           particleInterpolationEnabled;
    const bool engineHookEnabled = renderUnlockEnabled || interpolationClockEnabled;
    if (timingHooksEnabled && !QueryPerformanceFrequency(&g_qpcFrequency)) {
        error =
            L"QueryPerformanceFrequency failed with Win32 error " + std::to_wstring(GetLastError());
        return false;
    }
    if (motionTraceEnabled &&
        !motion_trace::Initialize(motionTracePath, g_qpcFrequency.QuadPart, error)) {
        return false;
    }

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        error = HookError(L"MH_Initialize", status);
        return false;
    }

    g_renderUnlockEnabled = renderUnlockEnabled;
    g_interpolationClockEnabled = interpolationClockEnabled;
    g_sdlGetTicks = reinterpret_cast<SdlGetTicks>(mapping.sdlGetTicks);

    if (engineHookEnabled) {
        if (renderUnlockEnabled) {
            g_renderUnlockState.store(RenderUnlockState::pending,
                                      std::memory_order_release);
        }
        status = MH_CreateHook(mapping.engineRun, &EngineRunDetour,
                               reinterpret_cast<void**>(&g_originalEngineRun));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cEngine::Run)", status);
            RollBackHooks(mapping);
            return false;
        }
    }

    if (timingHooksEnabled) {
        status = MH_CreateHook(mapping.updaterRunMessage, &UpdaterRunMessageDetour,
                               reinterpret_cast<void**>(&g_originalUpdaterRunMessage));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cUpdater::RunMessage)", status);
            RollBackHooks(mapping);
            return false;
        }
        status = MH_CreateHook(mapping.sceneRender, &SceneRenderDetour,
                               reinterpret_cast<void**>(&g_originalSceneRender));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cScene::Render)", status);
            RollBackHooks(mapping);
            return false;
        }
    }

    if (handsInterpolationEnabled || handsPoseInterpolationEnabled) {
        status = MH_CreateHook(mapping.playerHandsPostUpdate, &PlayerHandsPostUpdateDetour,
                               reinterpret_cast<void**>(&g_originalPlayerHandsPostUpdate));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cLuxPlayerHands::PostUpdate)", status);
            RollBackHooks(mapping);
            return false;
        }
    }

    if (worldInterpolationEnabled) {
        status = MH_CreateHook(mapping.luxPropOnUpdate, &LuxPropOnUpdateDetour,
                               reinterpret_cast<void**>(&g_originalLuxPropOnUpdate));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(iLuxProp::OnUpdate)", status);
            RollBackHooks(mapping);
            return false;
        }
        status = MH_CreateHook(mapping.newtonTransformCallback, &NewtonTransformCallbackDetour,
                               reinterpret_cast<void**>(&g_originalNewtonTransformCallback));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cPhysicsBodyNewton transform callback)", status);
            RollBackHooks(mapping);
            return false;
        }
    }

    if (worldInterpolationEnabled || worldPoseInterpolationEnabled) {
        status = MH_CreateHook(mapping.luxEnemyOnUpdate, &LuxEnemyOnUpdateDetour,
                               reinterpret_cast<void**>(&g_originalLuxEnemyOnUpdate));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(iLuxEnemy::OnUpdate)", status);
            RollBackHooks(mapping);
            return false;
        }
    }

    if (handsInterpolationEnabled || worldInterpolationEnabled) {
        status = MH_CreateHook(mapping.subMeshGetModelMatrix, &SubMeshGetModelMatrixDetour,
                               reinterpret_cast<void**>(&g_originalSubMeshGetModelMatrix));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cSubMeshEntity::GetModelMatrix)", status);
            RollBackHooks(mapping);
            return false;
        }
    }

    if (particleInterpolationEnabled) {
        status = MH_CreateHook(mapping.particleEmitterUpdateGraphics,
                               &ParticleEmitterUpdateGraphicsDetour,
                               reinterpret_cast<void**>(
                                   &g_originalParticleEmitterUpdateGraphics));
        if (status != MH_OK) {
            error = HookError(
                L"MH_CreateHook(iParticleEmitter::UpdateGraphicsForViewport)", status);
            RollBackHooks(mapping);
            return false;
        }
    }

    if (imageTrailTimingEnabled) {
        status = MH_CreateHook(mapping.imageTrailRenderEffect,
                               &ImageTrailRenderEffectDetour,
                               reinterpret_cast<void**>(
                                   &g_originalImageTrailRenderEffect));
        if (status != MH_OK) {
            error = HookError(
                L"MH_CreateHook(cPostEffect_ImageTrail::RenderEffect)", status);
            RollBackHooks(mapping);
            return false;
        }
    }

    if (mouseAuditEnabled) {
        status = MH_CreateHook(mapping.luxPlayerAddYaw, &LuxPlayerAddYawDetour,
                               reinterpret_cast<void**>(&g_originalLuxPlayerAddYaw));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cLuxPlayer::AddYaw)", status);
            RollBackHooks(mapping);
            return false;
        }
        status = MH_CreateHook(mapping.luxPlayerAddPitch, &LuxPlayerAddPitchDetour,
                               reinterpret_cast<void**>(&g_originalLuxPlayerAddPitch));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cLuxPlayer::AddPitch)", status);
            RollBackHooks(mapping);
            return false;
        }
        status = MH_CreateHook(mapping.cameraAddPitch, &CameraAddPitchDetour,
                               reinterpret_cast<void**>(&g_originalCameraAddPitch));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cCamera::AddPitch)", status);
            RollBackHooks(mapping);
            return false;
        }
        status = MH_CreateHook(mapping.cameraAddYaw, &CameraAddYawDetour,
                               reinterpret_cast<void**>(&g_originalCameraAddYaw));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cCamera::AddYaw)", status);
            RollBackHooks(mapping);
            return false;
        }
        status = MH_CreateHook(mapping.mouseSdlUpdate, &MouseSdlUpdateDetour,
                               reinterpret_cast<void**>(&g_originalMouseSdlUpdate));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cMouseSDL::Update)", status);
            RollBackHooks(mapping);
            return false;
        }
        status = MH_CreateHook(mapping.mouseSdlGetRelPosition, &MouseSdlGetRelPositionDetour,
                               reinterpret_cast<void**>(&g_originalMouseSdlGetRelPosition));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cMouseSDL::GetRelPosition)", status);
            RollBackHooks(mapping);
            return false;
        }
        status = MH_CreateHook(mapping.guiSendMousePos, &GuiSendMousePosDetour,
                               reinterpret_cast<void**>(&g_originalGuiSendMousePos));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cGui::SendMousePos)", status);
            RollBackHooks(mapping);
            return false;
        }
        status = MH_CreateHook(mapping.luxUpdateGamePlayerInput,
                               &LuxUpdateGamePlayerInputDetour,
                               reinterpret_cast<void**>(&g_originalLuxUpdateGamePlayerInput));
        if (status != MH_OK) {
            error = HookError(L"MH_CreateHook(cLuxInputHandler::UpdateGamePlayerInput)", status);
            RollBackHooks(mapping);
            return false;
        }
    }

    g_cameraInterpolationEnabled = cameraInterpolationEnabled;
    if (cameraInterpolationEnabled) {
        g_cameraRuntime.Configure({
            mapping.cameraSetPosition,
            mapping.cameraSetPitch,
            mapping.cameraSetYaw,
            mapping.cameraSetRoll,
        });
    }
    g_handsInterpolationEnabled = handsInterpolationEnabled;
    if (handsInterpolationEnabled) {
        g_handsRuntime.Configure({mapping.entityGetWorldMatrix,
                                  mapping.entityGetEntityParent});
    }
    g_worldInterpolationEnabled = worldInterpolationEnabled;
    if (worldInterpolationEnabled) {
        g_worldRuntime.Configure({mapping.entityGetWorldMatrix, mapping.entityGetEntityParent,
                                  mapping.physicsBodyGetEnabled});
        g_newtonBodyGetUserData =
            reinterpret_cast<NewtonBodyGetUserData>(mapping.newtonBodyGetUserData);
    }
    g_handsPoseInterpolationEnabled = handsPoseInterpolationEnabled;
    if (handsPoseInterpolationEnabled) {
        g_poseRuntime.Configure({mapping.meshEntityUpdateGraphics});
    }
    g_worldPoseInterpolationEnabled = worldPoseInterpolationEnabled;
    g_ragdollPoseInterpolationEnabled = ragdollPoseInterpolationEnabled;
    if (worldPoseInterpolationEnabled) {
        g_enemyPoseRuntime.Configure({mapping.meshEntityUpdateGraphics});
    }
    g_particleInterpolationEnabled = particleInterpolationEnabled;
    g_mousePresentationAcquisitionEnabled = mouseAuditEnabled;
    g_motionTraceEnabled = motionTraceEnabled;
    if (mouseAuditEnabled) {
        g_sdlGetRelativeMouseState = reinterpret_cast<SdlGetRelativeMouseState>(
            mapping.sdlGetRelativeMouseState);
        g_sdlPumpEvents = reinterpret_cast<SdlPumpEvents>(mapping.sdlPumpEvents);
    }

    if (engineHookEnabled) {
        status = MH_QueueEnableHook(mapping.engineRun);
    }
    if (status == MH_OK && timingHooksEnabled) {
        status = MH_QueueEnableHook(mapping.updaterRunMessage);
    }
    if (status == MH_OK && timingHooksEnabled) {
        status = MH_QueueEnableHook(mapping.sceneRender);
    }
    if (status == MH_OK && (handsInterpolationEnabled || handsPoseInterpolationEnabled)) {
        status = MH_QueueEnableHook(mapping.playerHandsPostUpdate);
    }
    if (status == MH_OK && worldInterpolationEnabled) {
        status = MH_QueueEnableHook(mapping.luxPropOnUpdate);
    }
    if (status == MH_OK && (worldInterpolationEnabled || worldPoseInterpolationEnabled)) {
        status = MH_QueueEnableHook(mapping.luxEnemyOnUpdate);
    }
    if (status == MH_OK && worldInterpolationEnabled) {
        status = MH_QueueEnableHook(mapping.newtonTransformCallback);
    }
    if (status == MH_OK && (handsInterpolationEnabled || worldInterpolationEnabled)) {
        status = MH_QueueEnableHook(mapping.subMeshGetModelMatrix);
    }
    if (status == MH_OK && particleInterpolationEnabled) {
        status = MH_QueueEnableHook(mapping.particleEmitterUpdateGraphics);
    }
    if (status == MH_OK && imageTrailTimingEnabled) {
        status = MH_QueueEnableHook(mapping.imageTrailRenderEffect);
    }
    if (status == MH_OK && mouseAuditEnabled) {
        status = MH_QueueEnableHook(mapping.luxPlayerAddYaw);
    }
    if (status == MH_OK && mouseAuditEnabled) {
        status = MH_QueueEnableHook(mapping.luxPlayerAddPitch);
    }
    if (status == MH_OK && mouseAuditEnabled) {
        status = MH_QueueEnableHook(mapping.cameraAddPitch);
    }
    if (status == MH_OK && mouseAuditEnabled) {
        status = MH_QueueEnableHook(mapping.cameraAddYaw);
    }
    if (status == MH_OK && mouseAuditEnabled) {
        status = MH_QueueEnableHook(mapping.mouseSdlUpdate);
    }
    if (status == MH_OK && mouseAuditEnabled) {
        status = MH_QueueEnableHook(mapping.mouseSdlGetRelPosition);
    }
    if (status == MH_OK && mouseAuditEnabled) {
        status = MH_QueueEnableHook(mapping.guiSendMousePos);
    }
    if (status == MH_OK && mouseAuditEnabled) {
        status = MH_QueueEnableHook(mapping.luxUpdateGamePlayerInput);
    }
    if (status == MH_OK) {
        status = MH_ApplyQueued();
    }
    if (status != MH_OK) {
        error = HookError(L"Enabling engine hooks", status);
        RollBackHooks(mapping);
        return false;
    }

    log::Write(log::Level::info,
               L"Validated engine mappings: cEngine::Run RVA 0x000E1420, "
               L"cUpdater::RunMessage RVA 0x000E4070, cScene::Render RVA 0x0012B690, "
               L"cCamera setters RVA 0x0011CC20/0x0011C690/0x0011C700/0x0011C770, "
               L"cCamera AddPitch/AddYaw RVA 0x0011C790/0x0011C810, "
               L"cLuxPlayer::AddYaw/AddPitch RVA 0x000B1262/0x000B131D, "
               L"cLuxPlayerHands::PostUpdate RVA 0x000B4E4F, "
               L"iLuxProp::OnUpdate RVA 0x00035881, "
               L"iLuxEnemy::OnUpdate RVA 0x00028FE6, "
               L"iEntity3D::GetWorldMatrix RVA 0x0011F300, "
               L"iEntity3D::GetEntityParent RVA 0x0011F370, "
               L"cPhysicsBodyNewton::GetEnabled RVA 0x001E78E0, "
               L"cPhysicsBodyNewton transform callback RVA 0x001E81D0, "
               L"cMeshEntity::UpdateGraphicsForFrame RVA 0x00101020, "
               L"iParticleEmitter::UpdateGraphicsForViewport RVA 0x00166A80, "
               L"cPostEffect_ImageTrail::RenderEffect RVA 0x001A9DF0, "
               L"cSubMeshEntity::GetModelMatrix RVA 0x00195CF0, "
               L"cMouseSDL::Update/GetRelPosition RVA 0x001D2100/0x001D20E0, "
               L"cGui::SendMousePos RVA 0x000FB4F0, "
               L"SDL2 relative-state/pump/ticks IAT RVA "
               L"0x005DDA68/0x005DDA6C/0x005DDA74, "
               L"cLuxInputHandler::UpdateGamePlayerInput RVA 0x00058BA7.");
    if (renderUnlockEnabled) {
        log::Write(log::Level::info,
                   L"Render-unlock hook enabled; awaiting cEngine::Run. Use VSync or an "
                   L"external frame cap during this milestone.");
    }

    if (cameraInterpolationEnabled) {
        log::Write(log::Level::info,
                   L"Main-view camera interpolation enabled: render-scoped position and "
                   L"quaternion-orientation/cache overrides will be restored "
                   L"byte-for-byte "
                   L"after cScene::Render.");
    }

    if (handsInterpolationEnabled) {
        log::Write(log::Level::info,
                   L"First-person hands-root interpolation enabled: completed-tick root "
                   L"matrices share the camera presentation clock; renderer-facing "
                   L"hand and attached-object submesh matrices are returned from "
                   L"plugin-owned fixed storage. Parent-validated hand-object "
                   L"billboard, particle, and light world-matrix caches receive the "
                   L"same render-scoped correction and are restored byte-for-byte "
                   L"after cScene::Render.");
    }

    if (worldInterpolationEnabled) {
        log::Write(log::Level::info,
                   L"Selected world-root interpolation enabled for dynamic Lux props, "
                   L"otherwise-unowned Newton render bodies, and Grunt enemies: "
                   L"completed-tick Newton-body/mesh roots share the camera presentation "
                   L"clock; renderer-facing submeshes select their actual parent-body "
                   L"history, sleeping Newton props use a cached fast path with immediate "
                   L"transform-callback wakeup, "
                   L"and results use "
                   L"plugin-owned storage. Physics and HPL transform trees are "
                   L"unchanged.");
    }

    if (handsPoseInterpolationEnabled) {
        log::Write(log::Level::info,
                   L"First-person hands-pose interpolation enabled: completed bone "
                   L"palettes are sampled on the shared presentation clock, exposed "
                   L"only during cScene::Render, and restored byte-for-byte afterward. "
                   L"Animation logic and events are not re-run.");
    }

    if (worldPoseInterpolationEnabled) {
        log::Write(log::Level::info,
                   L"Grunt pose interpolation enabled: active Grunt meshes use "
                   L"independent completed-tick affine palette histories on the shared "
                   L"presentation clock. Palettes are exposed only during "
                   L"cScene::Render and restored byte-for-byte; animation logic and "
                   L"events are not re-run.");
    }

    if (particleInterpolationEnabled) {
        log::Write(log::Level::info,
                   L"Particle position interpolation enabled: every visible emitter uses "
                   L"HPL's built-in previous/current fixed positions on the shared "
                   L"presentation clock. Positions are overridden only while that emitter "
                   L"builds its render vertices and restored byte-for-byte afterward; "
                   L"particle simulation, collision, emission, and lifetime remain fixed-step.");
    }

    if (imageTrailTimingEnabled) {
        log::Write(log::Level::info,
                   L"Image-trail timing correction enabled: HPL's stock 60 Hz "
                   L"accumulation-buffer decay is converted to the current render "
                   L"interval. The gameplay amount and effect fade remain fixed-step, "
                   L"and the original amount is restored after every effect draw.");
    }

    if (mouseAuditEnabled) {
        log::Write(log::Level::info,
                   L"Mouse presentation preview enabled for smooth-off gameplay: active "
                   L"render frames pump SDL events while a game-window WM_INPUT tap retains "
                   L"each native relative packet. The next fixed sample retires those counts "
                   L"exactly once, and a render-only camera offset combines the "
                   L"unpresented fraction of captured authoritative angular commands with "
                   L"still-pending motion. Pitch/yaw setters retain engine limits and the "
                   L"authoritative camera is restored after rendering. The exact camera "
                   L"preview rotation is also composed into the render-only hands root, "
                   L"active hand object, and validated light/billboard/particle children. "
                   L"GUI states receive the same pending native counts as a direct "
                   L"presentation-rate software-cursor update; the next fixed GUI update "
                   L"retires only the unpresented correction, with no cursor history.");
    }

    if (motionTraceEnabled) {
        log::Write(log::Level::info,
                   L"Asynchronous camera motion trace enabled: " + motionTracePath +
                       L". The render thread only publishes fixed-size records; the "
                       L"telemetry thread writes CSV batches.");
    }

    if (!telemetryEnabled && !motionTraceEnabled) {
        return true;
    }

    g_reportIntervalSeconds = reportIntervalSeconds;
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stopEvent == nullptr) {
        error = L"CreateEventW for telemetry reporter failed with Win32 error " +
                std::to_wstring(GetLastError());
        RollBackHooks(mapping);
        return false;
    }
    const HANDLE reporter = CreateThread(nullptr, 0, ReporterThread, nullptr, 0, nullptr);
    if (reporter == nullptr) {
        error = L"CreateThread for telemetry reporter failed with Win32 error " +
                std::to_wstring(GetLastError());
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        RollBackHooks(mapping);
        return false;
    }
    CloseHandle(reporter);

    log::Write(log::Level::info, L"Read-only telemetry hooks enabled; report interval " +
                                     std::to_wstring(reportIntervalSeconds) + L" seconds.");
    return true;
}

} // namespace atdd::telemetry
