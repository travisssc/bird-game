// Camera, flight aim and head look. Engine-agnostic, deterministic, see docs/FLIGHT_MODEL.md.
//
// Normal flight: the mouse moves the camera, and the bird steers toward where the camera looks.
// Holding free look (Alt): the flight aim freezes, so the bird holds its course while the mouse
// orbits the camera, and only the bird's head turns to look (within its neck limits). Releasing
// Alt swings the camera back behind the bird; the mouse steers again straight away.
//
// The Unreal side feeds raw mouse deltas in here and writes the camera angles to the controller's
// ControlRotation (instead of letting the controller integrate mouse input itself), passes
// GetAimDirection() to the flight model, and drives the neck bone from the head angles.
#pragma once

#include "FlightCore/FlightMath.h"
#include "FlightCore/FlightModel.h"

#include <cstdint>

namespace BirdFlight
{
	struct FLookParams
	{
		double PitchMinDeg = -85.0;           // camera and aim pitch limits
		double PitchMaxDeg = 85.0;
		double ReturnTimeSec = 0.3;           // camera is ~99% back behind the bird after this
		double NeckYawLimitDeg = 120.0;       // head turn either side of the beak (crows turn far)
		double NeckPitchUpDeg = 50.0;
		double NeckPitchDownDeg = 70.0;
		double HeadTurnRateDegPerSec = 720.0; // birds snap their heads around quickly
		double HeadLeadFraction = 0.5;        // normal flight: the head leads this much into the aim
	};

	enum class ELookMode : std::uint8_t
	{
		Flying,    // camera == flight aim
		FreeLook,  // Alt held: aim frozen, camera free, head follows camera
		Returning, // Alt released: camera swinging back to the aim
	};

	struct FLookState
	{
		double AimYawRad = 0.0;               // flight aim: what the bird steers toward
		double AimPitchRad = 0.0;
		double CameraYawRad = 0.0;            // where the camera looks
		double CameraPitchRad = 0.0;
		double HeadYawRad = 0.0;              // head relative to the body (+ = right), for the neck bone
		double HeadPitchRad = 0.0;            // + = up
		ELookMode Mode = ELookMode::Flying;

		FVec3 GetAimDirection() const;
		FVec3 GetCameraDirection() const;
	};

	struct FLookInput
	{
		double MouseYawDeg = 0.0;             // this frame's mouse delta, already scaled by sensitivity
		double MousePitchDeg = 0.0;           // + = look up
		bool bFreeLook = false;               // hold Alt
	};

	// Advance by DeltaSec. Body is the bird's current flight state (for the head angles).
	void UpdateLook(FLookState& State, const FLookInput& Input, const FFlightState& Body,
		const FLookParams& Params, double DeltaSec);

	// Direction expressed as yaw/pitch relative to the bird's body frame (Forward/Right/Up).
	void DirectionToBodyAngles(const FFlightState& Body, const FVec3& Direction, double& OutYawRad, double& OutPitchRad);
}
