#pragma once
#include <cstdint>
#include <string>
//  NOT A PASTE. Regenerate with tools/offsets_regen.py - a dumper header pasted
//  over this one deletes the ~20 fields the app reads that no dumper emits, and
//  its `constexpr` stops the live update from compiling.
//
//  Values below are the version-4310300497aa4917 dump merged into the app's field list.
//  The list is longer than the dump and, unlike it, mutable: RefreshOffsets()
//  writes these globals at launch (offsets_live.cpp takes their addresses in
//  kSlots[]). A field the dump answers 0x0 for keeps the value in this file,
//  because 0 is how the dumper says "not found" - the same rule the live parser
//  applies, and the reason a pasted zero becomes a read at address+0. The raw
//  dump for this version is docs/offsets_dump.txt. A line tagged "dump has no
//  value; kept" is knowingly one release behind - the dumper publishes 0 for it,
//  so this file keeps the previous cross-checked number - and the debug page
//  names the version that is loaded, which is the only honest read on it.
//
//  Source stamp from the pasted header:
//    Dumped With     : RbxDumperV2
//    Roblox Version  : version-4310300497aa4917
//    Dumper Version  : 2.2.4
//    Dumped At       : 06:24 15/09/2026 (GMT)
//    Total Offsets   : 392

namespace Offsets {
    inline std::string ClientVersion = "version-4310300497aa4917";

    namespace AirProperties {
         inline uintptr_t AirDensity = 0x18;
         inline uintptr_t GlobalWind = 0x3c;
    }

    namespace AnimationTrack {
         inline uintptr_t Animation = 0xb8;
         inline uintptr_t Animator = 0x110;
         inline uintptr_t IsPlaying = 0xa58;
         inline uintptr_t Looped = 0xe5;
         inline uintptr_t Speed = 0xd4;
         inline uintptr_t TimePosition = 0xd8;
    }

    namespace Animator {
         inline uintptr_t ActiveAnimations = 0xab0;
    }

    namespace Atmosphere {
         inline uintptr_t Color = 0xb8;
         inline uintptr_t Decay = 0xc4;
         inline uintptr_t Density = 0xd0;
         inline uintptr_t Glare = 0xd4;
         inline uintptr_t Haze = 0xd8;
         inline uintptr_t Offset = 0xdc;
    }

    namespace Attachment {
         inline uintptr_t Position = 0xc4;
    }

    namespace BasePart {
         inline uintptr_t CastShadow = 0x135;
         inline uintptr_t Color3 = 0x1a8;
         inline uintptr_t Locked = 0x136;
         inline uintptr_t Massless = 0x137;
         inline uintptr_t Primitive = 0x188;
         inline uintptr_t Reflectance = 0x10c;
         inline uintptr_t Shape = 0x1b8;
         inline uintptr_t Transparency = 0x130;
    }

    namespace Beam {
         inline uintptr_t Attachment0 = 0x160;
         inline uintptr_t Attachment1 = 0x170;
         inline uintptr_t Brightness = 0x180;
         inline uintptr_t CurveSize0 = 0x184;
         inline uintptr_t CurveSize1 = 0x188;
         inline uintptr_t LightEmission = 0x18c;
         inline uintptr_t LightInfluence = 0x190;
         inline uintptr_t Texture = 0x140;
         inline uintptr_t TextureLength = 0x19c;
         inline uintptr_t TextureSpeed = 0x1a4;
         inline uintptr_t Width0 = 0x1a8;
         inline uintptr_t Width1 = 0x1ac;
         inline uintptr_t ZOffset = 0x1b0;
    }

    namespace BloomEffect {
         inline uintptr_t Enabled = 0xb0;
         inline uintptr_t Intensity = 0xb8;
         inline uintptr_t Size = 0xbc;
         inline uintptr_t Threshold = 0xc0;
    }

    namespace BlurEffect {
         inline uintptr_t Enabled = 0xb0;
         inline uintptr_t Size = 0xb8;
    }

    namespace ByteCode {
         inline uintptr_t Pointer = 0x10;
         inline uintptr_t Size = 0x28;
    }

    namespace Camera {
         inline uintptr_t CameraSubject = 0xc8;
         inline uintptr_t CameraType = 0x138;
         inline uintptr_t FieldOfView = 0x140;
         inline uintptr_t ImagePlaneDepth = 0x2d4;
         inline uintptr_t Position = 0xfc;
         inline uintptr_t Rotation = 0xd8;
         inline uintptr_t Viewport = 0x28c;
         inline uintptr_t ViewportSize = 0x2cc;
    }

    namespace CharacterMesh {
         inline uintptr_t BaseTextureId = 0xc8;
         inline uintptr_t BodyPart = 0x148;
         inline uintptr_t MeshId = 0xf8;
         inline uintptr_t OverlayTextureId = 0x128;
    }

    namespace ClickDetector {
         inline uintptr_t MaxActivationDistance = 0xe8;
         inline uintptr_t MouseIcon = 0xc8;
    }

    namespace Clothing {
         inline uintptr_t Color3 = 0x120;
         inline uintptr_t Template = 0x100;
    }

    namespace ColorCorrectionEffect {
         inline uintptr_t Brightness = 0xc4;
         inline uintptr_t Contrast = 0xc8;
         inline uintptr_t Enabled = 0xb0;
         inline uintptr_t TintColor = 0xb8;
    }

    namespace ColorGradingEffect {
         inline uintptr_t Enabled = 0xb0;
         inline uintptr_t TonemapperPreset = 0xb8;
    }

    namespace DataModel {
         inline uintptr_t CreatorId = 0x188;
         inline uintptr_t GameId = 0x190;
         inline uintptr_t GameLoaded = 0x5e0;
         inline uintptr_t JobId = 0x120;
         inline uintptr_t PlaceId = 0x198;
         inline uintptr_t PlaceVersion = 0x1b4;
         inline uintptr_t PrimitiveCount = 0x428;
         inline uintptr_t ScriptContext = 0x440;
         inline uintptr_t ServerIP = 0x5c8;
         inline uintptr_t ToRenderView1 = 0x1d0;
         inline uintptr_t ToRenderView2 = 0x8;
         inline uintptr_t ToRenderView3 = 0x28;
         inline uintptr_t Workspace = 0x160;
    }

    namespace DepthOfFieldEffect {
         inline uintptr_t Enabled = 0xb0;
         inline uintptr_t FarIntensity = 0xb8;
         inline uintptr_t FocusDistance = 0xbc;
         inline uintptr_t InFocusRadius = 0xc0;
         inline uintptr_t NearIntensity = 0xc4;
    }

    namespace DragDetector {
         inline uintptr_t ActivatedCursorIcon = 0x1c0;
         inline uintptr_t CursorIcon = 0xc8;
         inline uintptr_t MaxActivationDistance = 0xe8;
         inline uintptr_t MaxDragAngle = 0x2a8;
         inline uintptr_t MaxDragTranslation = 0x26c;
         inline uintptr_t MaxForce = 0x2ac;
         inline uintptr_t MaxTorque = 0x2b0;
         inline uintptr_t MinDragAngle = 0x2b4;
         inline uintptr_t MinDragTranslation = 0x278;
         inline uintptr_t ReferenceInstance = 0x1f0;
         inline uintptr_t Responsiveness = 0x2c0;
    }

    namespace FakeDataModel {
         inline uintptr_t Pointer = 0x8e42c98;
         inline uintptr_t RealDataModel = 0x1f8;
    }

    namespace GuiBase2D {
         inline uintptr_t AbsolutePosition = 0x10c;
         inline uintptr_t AbsoluteRotation = 0xe8;
         inline uintptr_t AbsoluteSize = 0x114;   // dump has no value; kept
    }

    namespace GuiObject {
         inline uintptr_t BackgroundColor3 = 0x540;
         inline uintptr_t BackgroundTransparency = 0x54c;
         inline uintptr_t BorderColor3 = 0x54c;
         inline uintptr_t Image = 0x9a0;
         inline uintptr_t LayoutOrder = 0x57c;
         inline uintptr_t Position = 0x510;
         inline uintptr_t RichText = 0xb98;
         inline uintptr_t Rotation = 0xe8;
         inline uintptr_t ScreenGui_Enabled = 0x4c4;
         inline uintptr_t Size = 0x530;
         inline uintptr_t Text = 0xe00;
         inline uintptr_t TextColor3 = 0xeb0;
         inline uintptr_t Visible = 0x5ad;
         inline uintptr_t ZIndex = 0x5a4;
    }

    namespace Humanoid {
         inline uintptr_t AutoJumpEnabled = 0x1d4;
         inline uintptr_t AutoRotate = 0x1d5;
         inline uintptr_t AutomaticScalingEnabled = 0x1d6;
         inline uintptr_t BreakJointsOnDeath = 0xc5;
         inline uintptr_t CameraOffset = 0x128;
         inline uintptr_t DisplayDistanceType = 0x180;
         inline uintptr_t DisplayName = 0xb8;
         inline uintptr_t EvaluateStateMachine = 0x1d8;
         inline uintptr_t FloorMaterial = 0x184;
         inline uintptr_t Health = 0x190;
         inline uintptr_t HealthDisplayDistance = 0x188;
         inline uintptr_t HealthDisplayType = 0x18c;
         inline uintptr_t HipHeight = 0x194;
         // NOTE: reads back 0x0 on this client build (0.736). The root part is
         // resolved by NAME ("HumanoidRootPart") instead - see cache.cpp.
         inline uintptr_t HumanoidRootPart = 0x470;
         inline uintptr_t HumanoidState = 0x8b8;
         inline uintptr_t HumanoidStateID = 0x20;
         inline uintptr_t IsWalking = 0x95f;
         inline uintptr_t Jump = 0x1da;
         inline uintptr_t JumpHeight = 0x1a0;
         inline uintptr_t JumpPower = 0x1a4;
         inline uintptr_t MaxHealth = 0x1a8;
         inline uintptr_t MaxSlopeAngle = 0x1ac;
         inline uintptr_t MoveDirection = 0x140;
         inline uintptr_t MoveToPart = 0x118;
         inline uintptr_t MoveToPoint = 0x164;
         inline uintptr_t NameDisplayDistance = 0x1b0;
         inline uintptr_t NameOcclusion = 0x1b4;
         inline uintptr_t PlatformStand = 0x1dc;
         inline uintptr_t RequiresNeck = 0x1dd;
         inline uintptr_t RigType = 0x1c0;
         inline uintptr_t SeatPart = 0x108;
         inline uintptr_t Sit = 0x1dd;
         inline uintptr_t TargetPoint = 0x14c;
         inline uintptr_t UseJumpPower = 0x1e0;
         inline uintptr_t WalkTimer = 0x400;
         inline uintptr_t Walkspeed = 0x1d0;
         inline uintptr_t WalkspeedCheck = 0x3b4;
    }

    namespace Instance {
         inline uintptr_t ChildrenEnd = 0x8;
         inline uintptr_t ChildrenStart = 0x78;
         // Offset of the entry array INSIDE the container object, and bytes
         // per child entry. Windows layout: array at container+0, 16-byte
         // entries ({child, extra}) — memory.cpp reads raw[i*2]. On Sober the
         // shape is DISCOVERED (offsets_dynamic) and may be 8; zero/absent
         // stride means undiscovered and get_children returns empty.
         inline uintptr_t ChildrenArray = 0x0;
         inline uintptr_t ChildrenStride = 0x10;
         inline uintptr_t ClassBase = 0x1b0;
         inline uintptr_t ClassDescriptor = 0x18;
         inline uintptr_t ClassName = 0x8;
         inline uintptr_t Name = 0x8;
         inline uintptr_t NameContainer = 0x70;
         inline uintptr_t Parent = 0x68;
         inline uintptr_t This = 0x8;
    }

    namespace Lighting {
         inline uintptr_t Ambient = 0xd0;
         inline uintptr_t Brightness = 0x118;
         inline uintptr_t ClockTime = 0xc8;
         inline uintptr_t ColorShift_Bottom = 0xe8;
         inline uintptr_t ColorShift_Top = 0xdc;
         inline uintptr_t EnvironmentDiffuseScale = 0x11c;
         inline uintptr_t EnvironmentSpecularScale = 0x120;
         inline uintptr_t ExposureCompensation = 0x124;
         inline uintptr_t FogColor = 0xf4;
         inline uintptr_t FogEnd = 0x12c;
         inline uintptr_t FogStart = 0x130;
         inline uintptr_t GeographicLatitude = 0x134;
         inline uintptr_t GlobalShadows = 0x144;
         inline uintptr_t GradientBottom = 0x190;
         inline uintptr_t GradientTop = 0x150;
         inline uintptr_t LightColor = 0x15c;
         inline uintptr_t LightDirection = 0x168;
         inline uintptr_t MoonPosition = 0x184;
         inline uintptr_t OutdoorAmbient = 0x100;
         inline uintptr_t Sky = 0x1c8;
         inline uintptr_t Source = 0x174;
         inline uintptr_t SunPosition = 0x178;
    }

    namespace LocalScript {
         inline uintptr_t ByteCode = 0x190;
         inline uintptr_t GUID = 0xd0;
         inline uintptr_t Hash = 0x1a0;
    }

    namespace MaterialColors {
         inline uintptr_t Asphalt = 0x30;
         inline uintptr_t Basalt = 0x27;
         inline uintptr_t Brick = 0xf;
         inline uintptr_t Cobblestone = 0x33;
         inline uintptr_t Concrete = 0xc;
         inline uintptr_t CrackedLava = 0x2d;
         inline uintptr_t Glacier = 0x1b;
         inline uintptr_t Grass = 0x6;
         inline uintptr_t Ground = 0x2a;
         inline uintptr_t Ice = 0x36;
         inline uintptr_t LeafyGrass = 0x39;
         inline uintptr_t Limestone = 0x3f;
         inline uintptr_t Mud = 0x24;
         inline uintptr_t Pavement = 0x42;
         inline uintptr_t Rock = 0x18;
         inline uintptr_t Salt = 0x3c;
         inline uintptr_t Sand = 0x12;
         inline uintptr_t Sandstone = 0x21;
         inline uintptr_t Slate = 0x9;
         inline uintptr_t Snow = 0x1e;
         inline uintptr_t WoodPlanks = 0x15;
    }

    // The next two namespaces are not in a RbxDumperV2 header at all, so regenerating
    // leaves them alone: they came from the cross-check against the service JSON and
    // the Offsetmanager mirror. This version's dump describes the same cache under
    // other names (MeshContentProvider::LRUHolder 0xd8 -> LRUHolder::MemEnforcedLRUCache
    // 0x20 -> MemEnforcedLRUCache::Head 0x8 -> LRUNode{AssetID 0x10, CachedItem 0x40}
    // -> FileMeshData{Vertices 0x0, VerticesEnd 0x8, Faces 0x30, FacesEnd 0x38}) - and
    // the four mesh offsets in there are exactly MeshData's below, which is as far as
    // verification goes from a dump. If the inventory checker ever reads nothing after
    // an update, this naming split is the first place to look.
    namespace MeshContentProvider {
         inline uintptr_t AssetID = 0x10;
         inline uintptr_t Cache = 0xf0;
         inline uintptr_t LRUCache = 0x20;
         inline uintptr_t MeshData = 0x40;
         inline uintptr_t ToMeshData = 0x40;
    }

    namespace MeshData {
         inline uintptr_t FaceEnd = 0x38;
         inline uintptr_t FaceStart = 0x30;
         inline uintptr_t VertexEnd = 0x8;
         inline uintptr_t VertexStart = 0x0;
    }

    namespace MeshPart {
         inline uintptr_t MeshId = 0x310;
         inline uintptr_t Texture = 0x340;
    }

    namespace Misc {
         inline uintptr_t Adornee = 0xf0;
         inline uintptr_t AnimationId = 0xc0;
         inline uintptr_t StringLength = 0x10;
         inline uintptr_t Value = 0xb8;
    }

    namespace Model {
         inline uintptr_t PrimaryPart = 0x258;
         inline uintptr_t Scale = 0x144;
    }

    namespace ModuleScript {
         inline uintptr_t ByteCode = 0x138;
         inline uintptr_t GUID = 0xd0;
         inline uintptr_t Hash = 0x148;
         inline uintptr_t IsCoreScript = 0x0;
    }

    namespace MouseService {
         inline uintptr_t InputObject = 0xf0;
         inline uintptr_t InputObject2 = 0x100;
         inline uintptr_t MousePosition = 0xd4;
         inline uintptr_t SensitivityPointer = 0x307;   // dump has no value; kept
    }

    namespace ParticleEmitter {
         inline uintptr_t Acceleration = 0x1e0;
         inline uintptr_t Brightness = 0x21c;
         inline uintptr_t Drag = 0x220;
         inline uintptr_t Lifetime = 0x1f4;
         inline uintptr_t LightEmission = 0x238;
         inline uintptr_t LightInfluence = 0x23c;
         inline uintptr_t Rate = 0x248;
         inline uintptr_t RotSpeed = 0x1fc;
         inline uintptr_t Rotation = 0x204;
         inline uintptr_t Speed = 0x20c;
         inline uintptr_t SpreadAngle = 0x214;
         inline uintptr_t Texture = 0x1c0;
         inline uintptr_t TimeScale = 0x25c;
         inline uintptr_t VelocityInheritance = 0x260;
         inline uintptr_t ZOffset = 0x264;
    }

    namespace Player {
         inline uintptr_t AccountAge = 0x35c;
         inline uintptr_t CameraMode = 0x370;
         inline uintptr_t DisplayName = 0x138;
         inline uintptr_t HealthDisplayDistance = 0x394;
         inline uintptr_t LocalPlayer = 0x130;
         inline uintptr_t LocaleId = 0x118;
         inline uintptr_t MaxZoomDistance = 0x368;
         inline uintptr_t MinZoomDistance = 0x36c;
         inline uintptr_t ModelInstance = 0x298;
         inline uintptr_t Mouse = 0x1208;
         inline uintptr_t NameDisplayDistance = 0x3a4;
         inline uintptr_t Team = 0x2d8;
         inline uintptr_t TeamColor = 0x3b0;
         inline uintptr_t UserId = 0xd0;
    }

    namespace PlayerConfigurer {
         inline uintptr_t Pointer = 0x0;
    }

    namespace PlayerMouse {
         inline uintptr_t Icon = 0xc8;
         inline uintptr_t Workspace = 0x150;
    }

    namespace Primitive {
         inline uintptr_t AssemblyAngularVelocity = 0xec;
         inline uintptr_t AssemblyLinearVelocity = 0xe0;
         inline uintptr_t Flags = 0x1be;
         inline uintptr_t Material = 0x0;
         inline uintptr_t Owner = 0x218;
        // Reads come from block A and they are correct; the trap is on the write
        // side - a position written to A alone is re-synced back from B, so flight
        // and click teleport write both positions. That is what the float probe
        // confirmed, and it is why MotionBlock/Stride and the *2 fields exist.
        inline uintptr_t Position = 0xd4;
        inline uintptr_t Rotation = 0xb0;
        inline uintptr_t Size = 0x1c4;
        // THE PRIMITIVE HOLDS TWO IDENTICAL MOTION BLOCKS, not two CFrames. The float
        // probe on version-e7d81637d42c4b23 decoded this shape; this version's dump
        // still implies it, because Position 0xd4, Linear 0xe0 and Angular 0xec sit
        // exactly +0x24 / +0x30 / +0x3c from Rotation 0xb0:
        //
        //   block | rotation | position | linear vel | angular vel
        //   ------+----------+----------+------------+-------------
        //     A   |  0xB0    |  0xD4    |   0xE0     |   0xEC
        //     B   |  0xF8    |  0x11C   |   0x128    |   0x134
        //
        // stride = 0x48 = 36 (rot) + 12 (pos) + 12 (lin) + 12 (ang), exactly.
        //
        // Block B's LINEAR VELOCITY is live: the probe caught it holding
        // (1.42, 0, -15.94) - magnitude 16.0, the character's walkspeed - at the
        // same instant as block A. Writing only A's velocity (0xE0 now, 0xF8 then)
        // leaves block B holding the engine's value, and the engine re-syncs from
        // it. That is why the position write test alternates dY 10.00 / dY 0.00:
        // a race we lose about half the time.
        //
        // Treat these as ONE 0x48 block written whole, never as separate fields.
        // Only block A's offsets appear in any dump, so B's four are DERIVED from
        // A + stride by tools/offsets_regen.py - and checked against the dump's own
        // intra-block deltas first, which is what makes the derivation safe.
        inline uintptr_t MotionBlock  = 0xb0;   // == Rotation; start of block A
        inline uintptr_t MotionStride = 0x48;   // A -> B
        inline uintptr_t MotionSize   = 0x48;   // rot+pos+lin+ang
        // Offsets within a motion block.
        inline uintptr_t BlockRotation = 0x00;
        inline uintptr_t BlockPosition = 0x24;
        inline uintptr_t BlockLinear   = 0x30;
        inline uintptr_t BlockAngular  = 0x3c;

        inline uintptr_t CFrame2 = 0xf8;  // block B rotation
        inline uintptr_t Position2 = 0x11c;  // block B translation
        inline uintptr_t AssemblyLinearVelocity2 = 0x128;   // block B linear
        inline uintptr_t AssemblyAngularVelocity2 = 0x134;  // block B angular
        inline uintptr_t Validate = 0x6;
    }

    // Enum.Material.Air. Humanoid::FloorMaterial reads this - NOT zero - when the
    // character is not standing on anything, because a Roblox enum property cannot
    // hold an empty value. Testing it against 0 reports "airborne" as "grounded"
    // for every real floor material and gets the jump path backwards.
    namespace MaterialEnum {
         inline int Air = 1792;
    }

    namespace PrimitiveFlags {
         inline uintptr_t Anchored = 0x2;
         inline uintptr_t CanCollide = 0x8;
         inline uintptr_t CanQuery = 0x20;
         inline uintptr_t CanTouch = 0x10;
    }

    namespace ProximityPrompt {
         inline uintptr_t ActionText = 0xb0;
         inline uintptr_t Enabled = 0x136;
         inline uintptr_t GamepadKeyCode = 0x11c;
         inline uintptr_t HoldDuration = 0x120;
         inline uintptr_t KeyCode = 0x124;
         inline uintptr_t MaxActivationDistance = 0x128;
         inline uintptr_t ObjectText = 0xd0;
         inline uintptr_t RequiresLineOfSight = 0x137;
    }

    namespace RenderJob {
         inline uintptr_t FakeDataModel = 0x38;
         inline uintptr_t RealDataModel = 0x1f0;
         inline uintptr_t RenderView = 0x1d8;
    }

    namespace RenderView {
        // All four are tagged below as "dump has no value": the dumper publishes 0 for
        // them on 2.2.4, so these are the previous release's cross-checked numbers. That
        // matters more here than anywhere else in the file, because the skybox changer
        // WRITES the two validity bytes (it flips them to false so the renderer
        // re-uploads), and its only guard is that the address it lands on is mapped.
        // A moved RenderView therefore cannot crash, but it can flip a byte that means
        // something else. If the skybox stops applying after an update, this is the
        // first suspect - and the debug page's offsets row is the first question.
         inline uintptr_t DeviceD3D11 = 0x8;   // dump has no value; kept
         inline uintptr_t LightingValid = 0x150;   // dump has no value; kept
         inline uintptr_t SkyValid = 0x28d;   // dump has no value; kept
         inline uintptr_t VisualEngine = 0x10;   // dump has no value; kept
    }

    namespace RunService {
         inline uintptr_t HeartbeatFPS = 0xc0;
         inline uintptr_t HeartbeatTask = 0xe0;
    }

    namespace Script {
         inline uintptr_t ByteCode = 0x190;
         inline uintptr_t GUID = 0xd0;
         inline uintptr_t Hash = 0x1a0;
    }

    namespace ScriptContext {
         inline uintptr_t RequireBypass = 0x0;
    }

    namespace Seat {
         inline uintptr_t Occupant = 0x218;
    }

    namespace Sky {
         inline uintptr_t MoonAngularSize = 0x244;
         inline uintptr_t MoonTextureId = 0xc8;
         inline uintptr_t SkyboxBk = 0xf8;
         inline uintptr_t SkyboxDn = 0x128;
         inline uintptr_t SkyboxFt = 0x158;
         inline uintptr_t SkyboxLf = 0x188;
         inline uintptr_t SkyboxOrientation = 0x238;
         inline uintptr_t SkyboxRt = 0x1b8;
         inline uintptr_t SkyboxUp = 0x1e8;
         inline uintptr_t StarCount = 0x248;
         inline uintptr_t SunAngularSize = 0x23c;
         inline uintptr_t SunTextureId = 0x218;
    }

    namespace Sound {
         inline uintptr_t IsPlaying = 0x140;
         inline uintptr_t Looped = 0x13d;
         inline uintptr_t PlaybackSpeed = 0x11c;
         inline uintptr_t RollOffMaxDistance = 0x120;
         inline uintptr_t RollOffMinDistance = 0x124;
         inline uintptr_t SoundGroup = 0xe8;
         inline uintptr_t SoundId = 0xc8;
         inline uintptr_t Volume = 0x130;
    }

    namespace SpawnLocation {
         inline uintptr_t AllowTeamChangeOnTouch = 0x3d;
         inline uintptr_t Enabled = 0x1f1;
         inline uintptr_t ForcefieldDuration = 0x1e8;
         inline uintptr_t Neutral = 0x1f2;
         inline uintptr_t TeamColor = 0x1ec;
    }

    namespace SpecialMesh {
         inline uintptr_t MeshId = 0xf8;
         inline uintptr_t Scale = 0xc4;
    }

    namespace StatsItem {
         inline uintptr_t Value = 0xd66a;
    }

    namespace SunRaysEffect {
         inline uintptr_t Enabled = 0xb0;
         inline uintptr_t Intensity = 0xb8;
         inline uintptr_t Spread = 0xbc;
    }

    namespace SurfaceAppearance {
         inline uintptr_t AlphaMode = 0x1f0;
         inline uintptr_t Color = 0x1d8;
         inline uintptr_t ColorMap = 0xc8;
         inline uintptr_t EmissiveMaskContent = 0xf8;
         inline uintptr_t EmissiveStrength = 0x1f4;
         inline uintptr_t EmissiveTint = 0x1e4;
         inline uintptr_t MetalnessMap = 0x128;
         inline uintptr_t NormalMap = 0x158;
         inline uintptr_t RoughnessMap = 0x188;
    }

    namespace TaskScheduler {
         inline uintptr_t JobEnd = 0xd0;
         inline uintptr_t JobName = 0x18;
         inline uintptr_t JobStart = 0xc8;
         inline uintptr_t MaxFPS = 0xb0;
         inline uintptr_t Pointer = 0x8bdd8e8;
    }

    namespace Team {
         inline uintptr_t BrickColor = 0xb8;
    }

    namespace Terrain {
         inline uintptr_t GrassLength = 0x1f0;
         inline uintptr_t MaterialColors = 0x4b8;
         inline uintptr_t WaterColor = 0x1e0;
         inline uintptr_t WaterReflectance = 0x1f8;
         inline uintptr_t WaterTransparency = 0x1fc;
         inline uintptr_t WaterWaveSize = 0x200;
         inline uintptr_t WaterWaveSpeed = 0x204;
    }

    namespace Textures {
         inline uintptr_t Decal_Texture = 0x1e0;
         inline uintptr_t Texture_Texture = 0x1e0;
    }

    namespace Tool {
         inline uintptr_t CanBeDropped = 0x4b8;
         inline uintptr_t Enabled = 0x475;
         inline uintptr_t Grip = 0x4ac;
         inline uintptr_t ManualActivationOnly = 0x295;
         inline uintptr_t RequiresHandle = 0x4bb;
         inline uintptr_t TextureId = 0x360;
         inline uintptr_t Tooltip = 0x468;
    }

    namespace UnionOperation {
         inline uintptr_t AssetId = 0x310;
    }

    namespace UserInputService {
         inline uintptr_t WindowInputState = 0x2c0;
    }

    namespace VehicleSeat {
         inline uintptr_t MaxSpeed = 0x228;
         inline uintptr_t SteerFloat = 0x22c;
         inline uintptr_t ThrottleFloat = 0x230;
         inline uintptr_t Torque = 0x234;
         inline uintptr_t TurnSpeed = 0x238;
    }

    namespace VisualEngine {
         inline uintptr_t Dimensions = 0xb10;
         inline uintptr_t FakeDataModel = 0xaf0;
         inline uintptr_t Pointer = 0x846f768;
         inline uintptr_t RenderView = 0xc30;
         inline uintptr_t ViewMatrix = 0x1b0;
    }

    namespace Weld {
         inline uintptr_t Part0 = 0x118;
         inline uintptr_t Part1 = 0x128;
    }

    namespace WeldConstraint {
         inline uintptr_t Part0 = 0xb8;
         inline uintptr_t Part1 = 0xc8;
    }

    namespace WindowInputState {
         inline uintptr_t CapsLock = 0x40;
         inline uintptr_t CurrentTextBox = 0x48;
    }

    namespace Workspace {
         inline uintptr_t CurrentCamera = 0x4b8;
         inline uintptr_t DistributedGameTime = 0x4d8;
         inline uintptr_t ReadOnlyGravity = 0xa00;
         inline uintptr_t World = 0x410;
    }

    namespace World {
         inline uintptr_t AirProperties = 0x240;
         inline uintptr_t FallenPartsDestroyHeight = 0x220;
         inline uintptr_t Gravity = 0x22c;
         inline uintptr_t Primitives = 0x2b0;
         inline uintptr_t worldStepsPerSec = 0x728;
    }

}
