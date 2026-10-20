#include "offsets_live.h"

#include "platform/platform.h"
#include "offsets.h"
#include "globals.h"
#include "features/servers/servers_http.h"


#include <cctype>
#include <chrono>      // the fetch budget below
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace {

// -----------------------------------------------------------------------------
//  Where offsets come from, and in which order.
//
//  offsets.imtheo.lol is the canonical source, but it sits behind Cloudflare
//  bot protection that drops WinHTTP's TLS ClientHello (win32 12175), so it is
//  only ever the FIRST thing tried. The service's own docs
//  (offsets.imtheo.lol/docs/cpp-auto-update) document the flow this follows -
//  version check, compare, fetch, cache - plus two things worth using: the
//  pinned per-version path (/{VER}/offsets.json, deterministic: no race
//  between the version check and the dump), and the plain "OffsetsClient/1.0"
//  UA their examples send, which is what the imtheo requests present. The
//  GitHub mirrors are plain CDNs with no bot gate and carry the same dumps:
//  ltseverydayyou/rdd republishes the current Roblox version hash, and
//  Offsetmanager/Roblox-Internal-External-Offsets commits a per-version
//  External_Offsets.hpp minutes after Roblox updates. r.jina.ai is a reader
//  proxy that fetches with a real browser engine on their side - the one
//  route that beats Cloudflare at its own game when even curl's hello is not
//  enough.
//
//  Every URL is fetched with GetAny(): WinHTTP first, System32\curl.exe as the
//  second transport (different TLS fingerprint, same job).
// -----------------------------------------------------------------------------
struct Source { const char* name; const char* url; const char* ua; };

// Each is tried until one yields a "version-..." string. The first two JSON
// mirrors answer the hash in their first "Windows" member.
const Source kVersionSources[] = {
    { "imtheo",            "https://offsets.imtheo.lol/roblox/version", "OffsetsClient/1.0" },
    { "rdd (github raw)",  "https://raw.githubusercontent.com/ltseverydayyou/rdd/main/api/versions/current.json", nullptr },
    { "weao mirror",       "https://ltseverydayyou.github.io/.well-known/weao/versions.json", nullptr },
    { "imtheo via jina",   "https://r.jina.ai/https://offsets.imtheo.lol/roblox/version", nullptr },
};

// Each is tried until one yields a body that parses. "{VER}" in a URL is
// replaced with the live version string; sources that need it are skipped
// when no version source answered. The pinned imtheo path comes first: it is
// the dump FOR the version the chain just resolved, where /offsets.json is
// whatever the latest dump happens to be.
const Source kOffsetsSources[] = {
    { "imtheo-pinned",     "https://offsets.imtheo.lol/{VER}/offsets.json", "OffsetsClient/1.0" },
    { "imtheo",            "https://offsets.imtheo.lol/offsets.json", "OffsetsClient/1.0" },
    { "offsetmanager",     "https://raw.githubusercontent.com/Offsetmanager/Roblox-Internal-External-Offsets/main/Windows/{VER}/External_Offsets.hpp", nullptr },
    { "imtheo via jina",   "https://r.jina.ai/https://offsets.imtheo.lol/offsets.json", nullptr },
};

// Every field below is handed to the fetcher as a WRITE POINTER, which is the one hard
// requirement offsets.h cannot violate and the thing a pasted dumper header gets wrong (it
// declares its values constexpr, and it omits the ~20 fields this app reads that no dumper
// emits). glass-obsidian/tools/offsets_regen.py merges a dump into offsets.h instead of
// replacing it; that tripwire is the message for the day someone skips the tool.
static_assert(!std::is_const<decltype(Offsets::Humanoid::Health)>::value,
              "offsets.h: every value must be a writable `inline uintptr_t` - "
              "RefreshOffsets() writes them, so constexpr does not compile. Use "
              "glass-obsidian/tools/offsets_regen.py to fold a new dump in.");

#define SLOT(C, F) { #C "::" #F, &Offsets::C::F }

struct Slot { const char* key; uintptr_t* dst; };

const Slot kSlots[] = {
    SLOT(AirProperties, AirDensity),
    SLOT(AirProperties, GlobalWind),
    SLOT(AnimationTrack, Animation),
    SLOT(AnimationTrack, Animator),
    SLOT(AnimationTrack, IsPlaying),
    SLOT(AnimationTrack, Looped),
    SLOT(AnimationTrack, Speed),
    SLOT(AnimationTrack, TimePosition),
    SLOT(Animator, ActiveAnimations),
    SLOT(Atmosphere, Color),
    SLOT(Atmosphere, Decay),
    SLOT(Atmosphere, Density),
    SLOT(Atmosphere, Glare),
    SLOT(Atmosphere, Haze),
    SLOT(Atmosphere, Offset),
    SLOT(Attachment, Position),
    SLOT(BasePart, CastShadow),
    SLOT(BasePart, Color3),
    SLOT(BasePart, Locked),
    SLOT(BasePart, Massless),
    SLOT(BasePart, Primitive),
    SLOT(BasePart, Reflectance),
    SLOT(BasePart, Shape),
    SLOT(BasePart, Transparency),
    SLOT(Beam, Attachment0),
    SLOT(Beam, Attachment1),
    SLOT(Beam, Brightness),
    SLOT(Beam, CurveSize0),
    SLOT(Beam, CurveSize1),
    SLOT(Beam, LightEmission),
    SLOT(Beam, LightInfluence),
    SLOT(Beam, Texture),
    SLOT(Beam, TextureLength),
    SLOT(Beam, TextureSpeed),
    SLOT(Beam, Width0),
    SLOT(Beam, Width1),
    SLOT(Beam, ZOffset),
    SLOT(BloomEffect, Enabled),
    SLOT(BloomEffect, Intensity),
    SLOT(BloomEffect, Size),
    SLOT(BloomEffect, Threshold),
    SLOT(BlurEffect, Enabled),
    SLOT(BlurEffect, Size),
    SLOT(ByteCode, Pointer),
    SLOT(ByteCode, Size),
    SLOT(Camera, CameraSubject),
    SLOT(Camera, CameraType),
    SLOT(Camera, FieldOfView),
    SLOT(Camera, ImagePlaneDepth),
    SLOT(Camera, Position),
    SLOT(Camera, Rotation),
    SLOT(Camera, Viewport),
    SLOT(Camera, ViewportSize),
    SLOT(CharacterMesh, BaseTextureId),
    SLOT(CharacterMesh, BodyPart),
    SLOT(CharacterMesh, MeshId),
    SLOT(CharacterMesh, OverlayTextureId),
    SLOT(ClickDetector, MaxActivationDistance),
    SLOT(ClickDetector, MouseIcon),
    SLOT(Clothing, Color3),
    SLOT(Clothing, Template),
    SLOT(ColorCorrectionEffect, Brightness),
    SLOT(ColorCorrectionEffect, Contrast),
    SLOT(ColorCorrectionEffect, Enabled),
    SLOT(ColorCorrectionEffect, TintColor),
    SLOT(ColorGradingEffect, Enabled),
    SLOT(ColorGradingEffect, TonemapperPreset),
    SLOT(DataModel, CreatorId),
    SLOT(DataModel, GameId),
    SLOT(DataModel, GameLoaded),
    SLOT(DataModel, JobId),
    SLOT(DataModel, PlaceId),
    SLOT(DataModel, PlaceVersion),
    SLOT(DataModel, PrimitiveCount),
    SLOT(DataModel, ScriptContext),
    SLOT(DataModel, ServerIP),
    SLOT(DataModel, ToRenderView1),
    SLOT(DataModel, ToRenderView2),
    SLOT(DataModel, ToRenderView3),
    SLOT(DataModel, Workspace),
    SLOT(DepthOfFieldEffect, Enabled),
    SLOT(DepthOfFieldEffect, FarIntensity),
    SLOT(DepthOfFieldEffect, FocusDistance),
    SLOT(DepthOfFieldEffect, InFocusRadius),
    SLOT(DepthOfFieldEffect, NearIntensity),
    SLOT(DragDetector, ActivatedCursorIcon),
    SLOT(DragDetector, CursorIcon),
    SLOT(DragDetector, MaxActivationDistance),
    SLOT(DragDetector, MaxDragAngle),
    SLOT(DragDetector, MaxDragTranslation),
    SLOT(DragDetector, MaxForce),
    SLOT(DragDetector, MaxTorque),
    SLOT(DragDetector, MinDragAngle),
    SLOT(DragDetector, MinDragTranslation),
    SLOT(DragDetector, ReferenceInstance),
    SLOT(DragDetector, Responsiveness),
    SLOT(FakeDataModel, Pointer),
    SLOT(FakeDataModel, RealDataModel),
    SLOT(GuiBase2D, AbsolutePosition),
    SLOT(GuiBase2D, AbsoluteRotation),
    SLOT(GuiBase2D, AbsoluteSize),
    SLOT(GuiObject, BackgroundColor3),
    SLOT(GuiObject, BackgroundTransparency),
    SLOT(GuiObject, BorderColor3),
    SLOT(GuiObject, Image),
    SLOT(GuiObject, LayoutOrder),
    SLOT(GuiObject, Position),
    SLOT(GuiObject, RichText),
    SLOT(GuiObject, Rotation),
    SLOT(GuiObject, ScreenGui_Enabled),
    SLOT(GuiObject, Size),
    SLOT(GuiObject, Text),
    SLOT(GuiObject, TextColor3),
    SLOT(GuiObject, Visible),
    SLOT(GuiObject, ZIndex),
    SLOT(Humanoid, AutoJumpEnabled),
    SLOT(Humanoid, AutoRotate),
    SLOT(Humanoid, AutomaticScalingEnabled),
    SLOT(Humanoid, BreakJointsOnDeath),
    SLOT(Humanoid, CameraOffset),
    SLOT(Humanoid, DisplayDistanceType),
    SLOT(Humanoid, DisplayName),
    SLOT(Humanoid, EvaluateStateMachine),
    SLOT(Humanoid, FloorMaterial),
    SLOT(Humanoid, Health),
    SLOT(Humanoid, HealthDisplayDistance),
    SLOT(Humanoid, HealthDisplayType),
    SLOT(Humanoid, HipHeight),
    SLOT(Humanoid, HumanoidRootPart),
    SLOT(Humanoid, HumanoidState),
    SLOT(Humanoid, HumanoidStateID),
    SLOT(Humanoid, IsWalking),
    SLOT(Humanoid, Jump),
    SLOT(Humanoid, JumpHeight),
    SLOT(Humanoid, JumpPower),
    SLOT(Humanoid, MaxHealth),
    SLOT(Humanoid, MaxSlopeAngle),
    SLOT(Humanoid, MoveDirection),
    SLOT(Humanoid, MoveToPart),
    SLOT(Humanoid, MoveToPoint),
    SLOT(Humanoid, NameDisplayDistance),
    SLOT(Humanoid, NameOcclusion),
    SLOT(Humanoid, PlatformStand),
    SLOT(Humanoid, RequiresNeck),
    SLOT(Humanoid, RigType),
    SLOT(Humanoid, SeatPart),
    SLOT(Humanoid, Sit),
    SLOT(Humanoid, TargetPoint),
    SLOT(Humanoid, UseJumpPower),
    SLOT(Humanoid, WalkTimer),
    SLOT(Humanoid, Walkspeed),
    SLOT(Humanoid, WalkspeedCheck),
    SLOT(Instance, ChildrenEnd),
    SLOT(Instance, ChildrenStart),
    SLOT(Instance, ClassBase),
    SLOT(Instance, ClassDescriptor),
    SLOT(Instance, ClassName),
    SLOT(Instance, Name),
    SLOT(Instance, NameContainer),
    SLOT(Instance, Parent),
    SLOT(Instance, This),
    SLOT(Lighting, Ambient),
    SLOT(Lighting, Brightness),
    SLOT(Lighting, ClockTime),
    SLOT(Lighting, ColorShift_Bottom),
    SLOT(Lighting, ColorShift_Top),
    SLOT(Lighting, EnvironmentDiffuseScale),
    SLOT(Lighting, EnvironmentSpecularScale),
    SLOT(Lighting, ExposureCompensation),
    SLOT(Lighting, FogColor),
    SLOT(Lighting, FogEnd),
    SLOT(Lighting, FogStart),
    SLOT(Lighting, GeographicLatitude),
    SLOT(Lighting, GlobalShadows),
    SLOT(Lighting, GradientBottom),
    SLOT(Lighting, GradientTop),
    SLOT(Lighting, LightColor),
    SLOT(Lighting, LightDirection),
    SLOT(Lighting, MoonPosition),
    SLOT(Lighting, OutdoorAmbient),
    SLOT(Lighting, Sky),
    SLOT(Lighting, Source),
    SLOT(Lighting, SunPosition),
    SLOT(LocalScript, ByteCode),
    SLOT(LocalScript, GUID),
    SLOT(LocalScript, Hash),
    SLOT(MaterialColors, Asphalt),
    SLOT(MaterialColors, Basalt),
    SLOT(MaterialColors, Brick),
    SLOT(MaterialColors, Cobblestone),
    SLOT(MaterialColors, Concrete),
    SLOT(MaterialColors, CrackedLava),
    SLOT(MaterialColors, Glacier),
    SLOT(MaterialColors, Grass),
    SLOT(MaterialColors, Ground),
    SLOT(MaterialColors, Ice),
    SLOT(MaterialColors, LeafyGrass),
    SLOT(MaterialColors, Limestone),
    SLOT(MaterialColors, Mud),
    SLOT(MaterialColors, Pavement),
    SLOT(MaterialColors, Rock),
    SLOT(MaterialColors, Salt),
    SLOT(MaterialColors, Sand),
    SLOT(MaterialColors, Sandstone),
    SLOT(MaterialColors, Slate),
    SLOT(MaterialColors, Snow),
    SLOT(MaterialColors, WoodPlanks),
    SLOT(MeshContentProvider, AssetID),
    SLOT(MeshContentProvider, Cache),
    SLOT(MeshContentProvider, LRUCache),
    SLOT(MeshContentProvider, MeshData),
    SLOT(MeshContentProvider, ToMeshData),
    SLOT(MeshData, FaceEnd),
    SLOT(MeshData, FaceStart),
    SLOT(MeshData, VertexEnd),
    SLOT(MeshData, VertexStart),
    SLOT(MeshPart, MeshId),
    SLOT(MeshPart, Texture),
    SLOT(Misc, Adornee),
    SLOT(Misc, AnimationId),
    SLOT(Misc, StringLength),
    SLOT(Misc, Value),
    SLOT(Model, PrimaryPart),
    SLOT(Model, Scale),
    SLOT(ModuleScript, ByteCode),
    SLOT(ModuleScript, GUID),
    SLOT(ModuleScript, Hash),
    SLOT(ModuleScript, IsCoreScript),
    SLOT(MouseService, InputObject),
    SLOT(MouseService, InputObject2),
    SLOT(MouseService, MousePosition),
    SLOT(MouseService, SensitivityPointer),
    SLOT(ParticleEmitter, Acceleration),
    SLOT(ParticleEmitter, Brightness),
    SLOT(ParticleEmitter, Drag),
    SLOT(ParticleEmitter, Lifetime),
    SLOT(ParticleEmitter, LightEmission),
    SLOT(ParticleEmitter, LightInfluence),
    SLOT(ParticleEmitter, Rate),
    SLOT(ParticleEmitter, RotSpeed),
    SLOT(ParticleEmitter, Rotation),
    SLOT(ParticleEmitter, Speed),
    SLOT(ParticleEmitter, SpreadAngle),
    SLOT(ParticleEmitter, Texture),
    SLOT(ParticleEmitter, TimeScale),
    SLOT(ParticleEmitter, VelocityInheritance),
    SLOT(ParticleEmitter, ZOffset),
    SLOT(Player, AccountAge),
    SLOT(Player, CameraMode),
    SLOT(Player, DisplayName),
    SLOT(Player, HealthDisplayDistance),
    SLOT(Player, LocalPlayer),
    SLOT(Player, LocaleId),
    SLOT(Player, MaxZoomDistance),
    SLOT(Player, MinZoomDistance),
    SLOT(Player, ModelInstance),
    SLOT(Player, Mouse),
    SLOT(Player, NameDisplayDistance),
    SLOT(Player, Team),
    SLOT(Player, TeamColor),
    SLOT(Player, UserId),
    SLOT(PlayerConfigurer, Pointer),
    SLOT(PlayerMouse, Icon),
    SLOT(PlayerMouse, Workspace),
    SLOT(Primitive, AssemblyAngularVelocity),
    SLOT(Primitive, AssemblyLinearVelocity),
    SLOT(Primitive, Flags),
    SLOT(Primitive, Material),
    SLOT(Primitive, Owner),
    SLOT(Primitive, Position),
    SLOT(Primitive, Rotation),
    SLOT(Primitive, Size),
    SLOT(Primitive, CFrame2),
    SLOT(Primitive, Position2),
    SLOT(Primitive, AssemblyLinearVelocity2),
    SLOT(Primitive, AssemblyAngularVelocity2),
    SLOT(Primitive, Validate),
    SLOT(PrimitiveFlags, Anchored),
    SLOT(PrimitiveFlags, CanCollide),
    SLOT(PrimitiveFlags, CanQuery),
    SLOT(PrimitiveFlags, CanTouch),
    SLOT(ProximityPrompt, ActionText),
    SLOT(ProximityPrompt, Enabled),
    SLOT(ProximityPrompt, GamepadKeyCode),
    SLOT(ProximityPrompt, HoldDuration),
    SLOT(ProximityPrompt, KeyCode),
    SLOT(ProximityPrompt, MaxActivationDistance),
    SLOT(ProximityPrompt, ObjectText),
    SLOT(ProximityPrompt, RequiresLineOfSight),
    SLOT(RenderJob, FakeDataModel),
    SLOT(RenderJob, RealDataModel),
    SLOT(RenderJob, RenderView),
    SLOT(RenderView, DeviceD3D11),
    SLOT(RenderView, LightingValid),
    SLOT(RenderView, SkyValid),
    SLOT(RenderView, VisualEngine),
    SLOT(RunService, HeartbeatFPS),
    SLOT(RunService, HeartbeatTask),
    SLOT(Script, ByteCode),
    SLOT(Script, GUID),
    SLOT(Script, Hash),
    SLOT(ScriptContext, RequireBypass),
    SLOT(Seat, Occupant),
    SLOT(Sky, MoonAngularSize),
    SLOT(Sky, MoonTextureId),
    SLOT(Sky, SkyboxBk),
    SLOT(Sky, SkyboxDn),
    SLOT(Sky, SkyboxFt),
    SLOT(Sky, SkyboxLf),
    SLOT(Sky, SkyboxOrientation),
    SLOT(Sky, SkyboxRt),
    SLOT(Sky, SkyboxUp),
    SLOT(Sky, StarCount),
    SLOT(Sky, SunAngularSize),
    SLOT(Sky, SunTextureId),
    SLOT(Sound, IsPlaying),
    SLOT(Sound, Looped),
    SLOT(Sound, PlaybackSpeed),
    SLOT(Sound, RollOffMaxDistance),
    SLOT(Sound, RollOffMinDistance),
    SLOT(Sound, SoundGroup),
    SLOT(Sound, SoundId),
    SLOT(Sound, Volume),
    SLOT(SpawnLocation, AllowTeamChangeOnTouch),
    SLOT(SpawnLocation, Enabled),
    SLOT(SpawnLocation, ForcefieldDuration),
    SLOT(SpawnLocation, Neutral),
    SLOT(SpawnLocation, TeamColor),
    SLOT(SpecialMesh, MeshId),
    SLOT(SpecialMesh, Scale),
    SLOT(StatsItem, Value),
    SLOT(SunRaysEffect, Enabled),
    SLOT(SunRaysEffect, Intensity),
    SLOT(SunRaysEffect, Spread),
    SLOT(SurfaceAppearance, AlphaMode),
    SLOT(SurfaceAppearance, Color),
    SLOT(SurfaceAppearance, ColorMap),
    SLOT(SurfaceAppearance, EmissiveMaskContent),
    SLOT(SurfaceAppearance, EmissiveStrength),
    SLOT(SurfaceAppearance, EmissiveTint),
    SLOT(SurfaceAppearance, MetalnessMap),
    SLOT(SurfaceAppearance, NormalMap),
    SLOT(SurfaceAppearance, RoughnessMap),
    SLOT(TaskScheduler, JobEnd),
    SLOT(TaskScheduler, JobName),
    SLOT(TaskScheduler, JobStart),
    SLOT(TaskScheduler, MaxFPS),
    SLOT(TaskScheduler, Pointer),
    SLOT(Team, BrickColor),
    SLOT(Terrain, GrassLength),
    SLOT(Terrain, MaterialColors),
    SLOT(Terrain, WaterColor),
    SLOT(Terrain, WaterReflectance),
    SLOT(Terrain, WaterTransparency),
    SLOT(Terrain, WaterWaveSize),
    SLOT(Terrain, WaterWaveSpeed),
    SLOT(Textures, Decal_Texture),
    SLOT(Textures, Texture_Texture),
    SLOT(Tool, CanBeDropped),
    SLOT(Tool, Enabled),
    SLOT(Tool, Grip),
    SLOT(Tool, ManualActivationOnly),
    SLOT(Tool, RequiresHandle),
    SLOT(Tool, TextureId),
    SLOT(Tool, Tooltip),
    SLOT(UnionOperation, AssetId),
    SLOT(UserInputService, WindowInputState),
    SLOT(VehicleSeat, MaxSpeed),
    SLOT(VehicleSeat, SteerFloat),
    SLOT(VehicleSeat, ThrottleFloat),
    SLOT(VehicleSeat, Torque),
    SLOT(VehicleSeat, TurnSpeed),
    SLOT(VisualEngine, Dimensions),
    SLOT(VisualEngine, FakeDataModel),
    SLOT(VisualEngine, Pointer),
    SLOT(VisualEngine, RenderView),
    SLOT(VisualEngine, ViewMatrix),
    SLOT(Weld, Part0),
    SLOT(Weld, Part1),
    SLOT(WeldConstraint, Part0),
    SLOT(WeldConstraint, Part1),
    SLOT(WindowInputState, CapsLock),
    SLOT(WindowInputState, CurrentTextBox),
    SLOT(Workspace, CurrentCamera),
    SLOT(Workspace, DistributedGameTime),
    SLOT(Workspace, ReadOnlyGravity),
    SLOT(Workspace, World),
    SLOT(World, AirProperties),
    SLOT(World, FallenPartsDestroyHeight),
    SLOT(World, Gravity),
    SLOT(World, Primitives),
    SLOT(World, worldStepsPerSec),
};

#undef SLOT

// Class::Field -> the Offsets:: global it feeds. Built once per apply; the
// JSON and the header parser fill through the exact same table.
std::unordered_map<std::string, uintptr_t*> BuildSlotMap()
{
    std::unordered_map<std::string, uintptr_t*> map;
    map.reserve(sizeof(kSlots) / sizeof(kSlots[0]));
    for (const Slot& s : kSlots) map[s.key] = s.dst;
    return map;
}

void SkipWs(const std::string& s, size_t& i)
{
    while (i < s.size() && isspace((unsigned char)s[i])) ++i;
}

bool ParseString(const std::string& s, size_t& i, std::string& out)
{
    SkipWs(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\' && i < s.size()) {
            out.push_back(s[i++]);
            continue;
        }
        out.push_back(c);
    }
    return false;
}

bool ParseNumber(const std::string& s, size_t& i, unsigned long long& out)
{
    SkipWs(s, i);
    if (i >= s.size() || !isdigit((unsigned char)s[i])) return false;
    unsigned long long v = 0;
    while (i < s.size() && isdigit((unsigned char)s[i])) {
        v = v * 10ull + (unsigned)(s[i] - '0');
        ++i;
    }
    out = v;
    return true;
}

bool SkipValue(const std::string& s, size_t& i)
{
    SkipWs(s, i);
    if (i >= s.size()) return false;
    if (s[i] == '"') { std::string t; return ParseString(s, i, t); }
    if (s[i] == '{') {
        int d = 0; bool str = false;
        for (; i < s.size(); ++i) {
            char c = s[i];
            if (str) { if (c == '\\') { ++i; continue; } if (c == '"') str = false; continue; }
            if (c == '"') { str = true; continue; }
            if (c == '{') ++d;
            else if (c == '}') { if (--d == 0) { ++i; return true; } }
        }
        return false;
    }
    if (s[i] == '[') {
        int d = 0; bool str = false;
        for (; i < s.size(); ++i) {
            char c = s[i];
            if (str) { if (c == '\\') { ++i; continue; } if (c == '"') str = false; continue; }
            if (c == '"') { str = true; continue; }
            if (c == '[') ++d;
            else if (c == ']') { if (--d == 0) { ++i; return true; } }
        }
        return false;
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') ++i;
    return true;
}

// Offsets[Class][Field] -> number. Nested one extra object only.
bool ApplyOffsetsJson(const std::string& body, int& applied, int& kept_zero)
{
    applied = 0;
    kept_zero = 0;
    size_t p = body.find("\"Offsets\"");
    if (p == std::string::npos) return false;
    p = body.find('{', p);
    if (p == std::string::npos) return false;
    ++p;

    std::unordered_map<std::string, uintptr_t*> map = BuildSlotMap();

    while (p < body.size()) {
        SkipWs(body, p);
        if (p < body.size() && body[p] == '}') break;
        if (p < body.size() && body[p] == ',') { ++p; continue; }
        std::string cls;
        if (!ParseString(body, p, cls)) return false;
        SkipWs(body, p);
        if (p >= body.size() || body[p] != ':') return false;
        ++p;
        SkipWs(body, p);
        if (p >= body.size() || body[p] != '{') { if (!SkipValue(body, p)) return false; continue; }
        ++p;
        while (p < body.size()) {
            SkipWs(body, p);
            if (p < body.size() && body[p] == '}') { ++p; break; }
            if (p < body.size() && body[p] == ',') { ++p; continue; }
            std::string field;
            if (!ParseString(body, p, field)) return false;
            SkipWs(body, p);
            if (p >= body.size() || body[p] != ':') return false;
            ++p;
            unsigned long long n = 0;
            SkipWs(body, p);
            if (p < body.size() && isdigit((unsigned char)body[p])) {
                if (!ParseNumber(body, p, n)) return false;
            } else {
                if (!SkipValue(body, p)) return false;
                continue;
            }
            const std::string key = cls + "::" + field;
            auto it = map.find(key);
            if (it == map.end()) continue;
            if (n == 0) { ++kept_zero; continue; }   // dump 0x0: keep compiled default
            *it->second = (uintptr_t)n;
            ++applied;
        }
    }

    size_t rv = body.find("\"Roblox Version\"");
    if (rv != std::string::npos) {
        rv = body.find(':', rv);
        std::string ver;
        if (rv != std::string::npos) {
            size_t i = rv + 1;
            if (ParseString(body, i, ver) && !ver.empty())
                Offsets::ClientVersion = ver;
        }
    }
    return true;
}

std::string Trim(std::string s)
{
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && isspace((unsigned char)s[i])) ++i;
    return s.substr(i);
}

// The Offsetmanager mirror serves the dump as a C++ header - the same shape as
// this project's own offsets.h:
//
//     namespace Offsets {
//         inline std::string ClientVersion = "version-...";
//         namespace TaskScheduler {
//             inline constexpr uintptr_t Pointer = 0x8b5cee8;
//         }
//     }
//
// `inline` and `inline constexpr` are both accepted, values are 0x-hex or
// decimal, and dump-zeros keep the compiled default exactly like the JSON
// path does.
bool ApplyOffsetsHpp(const std::string& body, int& applied, int& kept_zero)
{
    applied = 0;
    kept_zero = 0;
    std::unordered_map<std::string, uintptr_t*> map = BuildSlotMap();
    std::string ns;
    size_t i = 0;
    while (i < body.size()) {
        size_t e = body.find('\n', i);
        if (e == std::string::npos) e = body.size();
        std::string line = body.substr(i, e - i);
        i = e + 1;

        const size_t b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos) continue;
        const size_t en = line.find_last_not_of(" \t\r");
        line = line.substr(b, en - b + 1);

        if (line.compare(0, 9, "namespace") == 0) {
            size_t p = 9;
            while (p < line.size() && isspace((unsigned char)line[p])) ++p;
            const size_t id_start = p;
            while (p < line.size() && (isalnum((unsigned char)line[p]) || line[p] == '_')) ++p;
            ns = line.substr(id_start, p - id_start);
            continue;
        }
        if (line == "}" || line == "};") { ns.clear(); continue; }

        if (line.compare(0, 6, "inline") != 0) continue;

        if (line.find("ClientVersion") != std::string::npos) {
            const size_t q1 = line.find('"');
            const size_t q2 = (q1 == std::string::npos) ? std::string::npos : line.find('"', q1 + 1);
            if (q2 != std::string::npos && q2 > q1 + 1)
                Offsets::ClientVersion = line.substr(q1 + 1, q2 - q1 - 1);
            continue;
        }

        static const char kNeedle[] = "uintptr_t ";
        const size_t p = line.find(kNeedle);
        if (p == std::string::npos) continue;
        const size_t name_start = p + sizeof(kNeedle) - 1;
        const size_t eq = line.find('=', name_start);
        if (eq == std::string::npos || ns.empty()) continue;
        std::string field = line.substr(name_start, eq - name_start);
        while (!field.empty() && isspace((unsigned char)field.back())) field.pop_back();
        if (field.empty()) continue;

        size_t v = eq + 1;
        while (v < line.size() && isspace((unsigned char)line[v])) ++v;
        if (v >= line.size() || !isxdigit((unsigned char)line[v])) continue;
        const size_t vend = line.find(';', v);
        if (vend == std::string::npos) continue;
        std::string val = line.substr(v, vend - v);
        const char* c = val.c_str();
        char* end = nullptr;
        const unsigned long long n = strtoull(c, &end, 0);   // base 0: 0x.. and decimal both
        if (end == c) continue;

        const std::string key = ns + "::" + field;
        auto it = map.find(key);
        if (it == map.end()) continue;
        if (n == 0) { ++kept_zero; continue; }
        *it->second = (uintptr_t)n;
        ++applied;
    }
    return applied > 0;
}

// One front door for both dump formats, so the cache file and the bundled
// fallback can be either one. Reader-proxied JSON (r.jina.ai) arrives with a
// few lines of markdown preamble; the JSON itself starts at the first '{'.
// `fmt` names the format that won, for the log line.
bool ApplyOffsetsBody(const std::string& body_in, const char*& fmt, int& applied, int& kept_zero)
{
    fmt = nullptr;
    applied = 0;
    kept_zero = 0;

    if (body_in.find("namespace Offsets") != std::string::npos) {
        fmt = "hpp";
        return ApplyOffsetsHpp(body_in, applied, kept_zero);
    }
    if (body_in.find("\"Offsets\"") != std::string::npos) {
        std::string body = body_in;
        const size_t brace = body.find('{');
        if (brace != std::string::npos && brace > 0)
            body = body.substr(brace);
        fmt = "json";
        return ApplyOffsetsJson(body, applied, kept_zero);
    }
    return false;
}

// Pulls a "version-..." string out of a reply, for the three shapes the
// version sources actually answer with: the bare text endpoint, the JSON
// mirrors (hash in the first "Windows" member), and a reader-proxied mix of
// both.
std::string ExtractVersion(const std::string& body)
{
    const std::string trimmed = Trim(body);

    // shape 1: the whole reply is the version string
    if (trimmed.rfind("version-", 0) == 0) {
        const size_t e = trimmed.find_first_of(" \t\r\n\"'");
        return trimmed.substr(0, e);
    }
    // shape 2: JSON, first "Windows" member holds the hash
    size_t p = body.find("\"Windows\"");
    if (p != std::string::npos) {
        p = body.find('"', p + 8);
        if (p != std::string::npos) {
            ++p;
            const size_t e = body.find('"', p);
            if (e != std::string::npos) {
                const std::string v = body.substr(p, e - p);
                if (v.rfind("version-", 0) == 0) return v;
            }
        }
    }
    // shape 3: the string is in there somewhere (reader preamble in front)
    p = body.find("version-");
    if (p != std::string::npos) {
        const size_t e = body.find_first_of(" \t\r\n\"'", p);
        return body.substr(p, e - p);
    }
    return std::string();
}

// (ExeDir moved into the platform layer - GetModuleFileNameA on Windows,
// /proc/self/exe on Linux. Same trailing-separator contract.)
bool ReadFile(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

bool WriteFile(const std::string& path, const std::string& body)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << body;
    return (bool)f;
}

} // namespace

// Snapshots for the UI, taken once by RefreshOffsets() below. They are file-scope rather
// than arguments because the launch order wants the fetch to run before anything reads
// them, and a caller that forgot to look should still see "(unknown)" rather than a
// half-filled struct.
std::string s_offsets_loaded;
std::string s_offsets_client;

bool RefreshOffsetsImpl()
{
    // 0) A wall-clock budget for the whole network phase, checked before each attempt
    //    (never in the middle of one: a request already in flight runs out its own
    //    timeouts). This function is the FIRST thing WinMain does - before the unhook,
    //    before the window exists, before anything is attached - so on a machine that
    //    cannot reach these hosts (no egress, a TLS-intercepting AV, a firewall that
    //    drops the probe instead of refusing it) "try every source" costs every timeout
    //    multiplied: eight URLs, each WinHTTP (8s resolve, 8s connect, 15s send, 15s
    //    receive) and then curl.exe. That is minutes in which the overlay does not exist
    //    and the log says nothing but "started", which is indistinguishable from a hang.
    //    Stopping early is not a gamble, because the bundled fallback is generated from
    //    offsets.h for the client this build was made against: the budget costs
    //    freshness, never the ability to read the game.
    constexpr int kFetchBudgetMs = 25000;
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    auto spent = [&t0]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - t0).count() > kFetchBudgetMs;
    };

    const std::string dir = platform::ExeDir();
    const std::string ver_path = dir + "offsets.version";
    const std::string json_path = dir + "offsets.json";

    // 1) What version is live right now? First source that answers a
    //    "version-..." string wins. imtheo is tried first but is the one
    //    behind Cloudflare; the GitHub mirrors carry the same hash.
    std::string live;
    for (const Source& src : kVersionSources) {
        if (spent()) {
            LogLine("offsets: %dms of network budget spent, no version source answered"
                    " - the bundled set stays and the rest are skipped", kFetchBudgetMs);
            break;
        }
        std::string body, err;
        if (!features::servershttp::GetAny(src.url, body, err, src.ua)) {
            LogLine("offsets: version via %s failed (%s)", src.name, err.c_str());
            continue;
        }
        const std::string v = ExtractVersion(body);
        if (v.empty()) {
            LogLine("offsets: %s answered, but no version string in it", src.name);
            continue;
        }
        live = v;
        s_offsets_client = live;
        LogLine("offsets: live version %s (via %s)", live.c_str(), src.name);
        break;
    }

    std::string cached_ver;
    {
        std::ifstream f(ver_path);
        if (f) std::getline(f, cached_ver);
        cached_ver = Trim(cached_ver);
    }

    std::string json;
    bool have_json = false;
    bool fresh = false;    // true when a source was downloaded AND applied below

    // 2) A cached dump counts only when it was saved for the version we just
    //    saw. When no version source answered, the cache is trusted blind -
    //    there is nothing to compare it against.
    if (!live.empty() && live == cached_ver && ReadFile(json_path, json)) {
        have_json = true;
        LogLine("offsets: cache is current (%s)", live.c_str());
    } else if (live.empty() && ReadFile(json_path, json)) {
        have_json = true;
        LogLine("offsets: no version source answered - trusting the cache (%s)",
                cached_ver.empty() ? "unknown version" : cached_ver.c_str());
    }

    // 3) Download when the cache is missing or stale. Sources that need the
    //    version string for their URL are skipped when we never got one.
    if (!have_json) {
        for (const Source& src : kOffsetsSources) {
            if (spent()) {
                LogLine("offsets: %dms of network budget spent after %s - falling back"
                        " to the cache / bundled file rather than trying the rest",
                        kFetchBudgetMs, src.name);
                break;
            }
            std::string url = src.url;
            const size_t vpos = url.find("{VER}");
            if (vpos != std::string::npos) {
                if (live.empty()) continue;
                url.replace(vpos, 5, live);
            }
            std::string body, err;
            if (!features::servershttp::GetAny(url.c_str(), body, err, src.ua)) {
                LogLine("offsets: %s failed (%s)", src.name, err.c_str());
                continue;
            }
            const char* fmt = nullptr;
            int applied = 0, kept = 0;
            if (!ApplyOffsetsBody(body, fmt, applied, kept) || applied == 0) {
                LogLine("offsets: %s answered, but the body did not parse", src.name);
                continue;
            }
            json = body;
            have_json = true;
            fresh = true;
            WriteFile(json_path, json);
            if (!Offsets::ClientVersion.empty())
                WriteFile(ver_path, Offsets::ClientVersion + "\n");
            LogLine("offsets: downloaded %s via %s (%s, %u bytes, %d fields, %d dump-zeros kept)",
                    Offsets::ClientVersion.c_str(), src.name, fmt ? fmt : "?",
                    (unsigned)json.size(), applied, kept);
            break;
        }
    }

    // 4) Nothing downloaded: the bundled offsets_fallback.json ships with the
    //    build and is newer than whatever the machine last cached.
    if (!have_json || json.empty()) {
        const std::string fallback_path = dir + "offsets_fallback.json";
        if (ReadFile(fallback_path, json)) {
            have_json = true;
            LogLine("offsets: using bundled fallback (%s)", fallback_path.c_str());
        }
    }

    // 5) Last resort before compiled defaults: a cache we already know is
    //    stale. Wrong-version offsets fail every read, but they fail louder
    //    than no offsets at all - and this is the one case where the log can
    //    say exactly what is wrong.
    if (!have_json || json.empty()) {
        if (ReadFile(json_path, json)) {
            have_json = true;
            LogLine("offsets: stale cache for %s while live is %s - offsets will be wrong",
                    cached_ver.empty() ? "(unknown)" : cached_ver.c_str(),
                    live.empty() ? "(unknown)" : live.c_str());
        }
    }

    if (!have_json || json.empty()) {
        LogLine("offsets: no json, compiled defaults for %s", Offsets::ClientVersion.c_str());
        return false;
    }

    // A fresh download already applied itself while being validated; the
    // cache and the fallback apply here.
    if (fresh) {
        if (!live.empty() && Offsets::ClientVersion != live)
            LogLine("offsets: WARNING - downloaded %s but the live client is %s",
                    Offsets::ClientVersion.c_str(), live.c_str());
        return true;
    }

    const char* fmt = nullptr;
    int applied = 0, kept = 0;
    if (!ApplyOffsetsBody(json, fmt, applied, kept) || applied == 0) {
        LogLine("offsets: body did not parse, compiled defaults stay");
        return false;
    }
    // If we knew the live version and still ended up on a dump for another
    // one, say so - silent wrong offsets are how "attached but every read
    // fails" looks from the outside.
    if (!live.empty() && Offsets::ClientVersion != live) {
        LogLine("offsets: WARNING - applied %s but the live client is %s",
                Offsets::ClientVersion.c_str(), live.c_str());
    }
    LogLine("offsets: applied %d fields for %s (%s, %d dump-zeros kept)",
            applied, Offsets::ClientVersion.c_str(), fmt ? fmt : "?", kept);
    return true;
}

// The one place that snapshots, so no return path above can forget to. Whatever the
// fetch decided - applied, kept, or nothing found - this is what the menu will say.
bool RefreshOffsets()
{
    const bool ok = RefreshOffsetsImpl();
    s_offsets_loaded = Offsets::ClientVersion;
    return ok;
}

const char* OffsetsLoadedVersion() { return s_offsets_loaded.c_str(); }
const char* OffsetsClientVersion() { return s_offsets_client.c_str(); }

