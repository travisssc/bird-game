// Engine-agnostic bird flight model. See docs/FLIGHT_MODEL.md for the design.
//
// The model is a "sim-lite" hybrid:
//  - Orientation is player-driven: the nose turns toward the aim direction at a limited rate,
//    and the bird banks automatically into turns.
//  - Motion is physics-driven: lift, drag, flap thrust, gravity and wind act on velocity.
//    Real aerodynamics (angle of attack, stall, induced drag) decide what that orientation
//    does to the bird's energy.
//
// Collision, ground movement and networking live in the Unreal wrapper, not here.
#pragma once

#include "FlightCore/FlightMath.h"

namespace BirdFlight
{
	// Tunable per-species parameters. Defaults are a crow (Corvus), roughly real-world.
	struct FFlightParams
	{
		// --- Body / environment ---
		double MassKg = 0.45;
		double Gravity = 9.81;                // m/s^2
		double AirDensity = 1.225;            // kg/m^3

		// --- Wings (fully extended) ---
		double WingAreaM2 = 0.12;
		double AspectRatio = 6.75;            // span^2 / area
		double OswaldEfficiency = 0.8;
		double LiftSlopePerRad = 4.5;         // dCL/d(alpha) for the finite wing
		double MaxLiftCoeff = 1.4;            // CL at stall
		double PostStallLiftFraction = 0.55;  // CL settles to this fraction of max once fully stalled
		double StallFalloffRad = 0.30;        // alpha range beyond stall over which CL decays
		double WingParasiticDragCoeff = 0.025;// profile drag, scales with wing extension
		double FlatPlateDragCoeff = 1.2;      // extra drag ~ sin^2(alpha), dominant when stalled
		double BodyDragAreaM2 = 0.0016;       // Cd*A of the body alone (sets tucked dive speed)
		double SideForceAreaM2PerRad = 0.04;  // tail/body side force; aligns velocity with heading

		// --- Wing pose ---
		double RelaxedExtension = 0.5;        // wing extension with no button held in the air
		double ExtensionRatePerSec = 5.0;     // how fast wings open/tuck (1.0 = full range)

		// --- Flapping ---
		double FlapThrustN = 3.0;             // forward thrust at zero airspeed
		double FlapThrustFadeSpeed = 24.0;    // m/s where thrust reaches zero (caps flapping speed)
		double FlapLiftN = 5.5;               // extra lift at zero airspeed (allows takeoff/hover-ish)
		double FlapLiftFadeSpeed = 8.0;       // m/s where flap lift reaches zero
		double FlapFrequencyHz = 4.0;         // wingbeat rate; also drives animation phase
		double FlapPulseAmount = 0.5;         // 0 = smooth thrust, 1 = strong per-beat pulses

		// --- Control ---
		double MaxTurnRateDegPerSec = 150.0;  // nose rotation rate with wings fully out
		double TuckedTurnRateScale = 0.35;    // turn rate multiplier with wings fully tucked
		double MaxBankDeg = 65.0;
		double BankRateDegPerSec = 240.0;
		double StallNoseDrop = 0.6;           // 0..1, how strongly a stall pulls the nose toward the airflow

		// --- Integration ---
		double MaxSubstepSec = 1.0 / 120.0;
		double MaxStepSec = 0.25;             // longer frames are clamped (hitch protection)
	};

	// Full simulation state. Plain data, so it can be copied, replicated and replayed.
	struct FFlightState
	{
		FVec3 Position;                       // m
		FVec3 Velocity;                       // m/s, world frame
		FVec3 Forward = FVec3(1.0, 0.0, 0.0); // unit, nose direction
		FVec3 Up = FVec3(0.0, 0.0, 1.0);      // unit, back of the bird (orthogonal to Forward)
		double BankRad = 0.0;                 // + = right wing down
		double WingExtension = 0.5;           // 0 = tucked, 1 = fully spread
		double FlapPhase = 0.0;               // 0..1 wingbeat cycle, advances only while flapping

		FVec3 GetRight() const { return Cross(Up, Forward); }
	};

	struct FFlightInput
	{
		FVec3 AimDirection = FVec3(1.0, 0.0, 0.0); // where the player wants the nose (camera forward)
		bool bGlide = false;                       // hold Space
		bool bFlap = false;                        // hold Left Mouse
		bool bTuck = false;                        // hold Shift
	};

	// Derived values from the last substep, for HUD, animation, audio, camera and tests.
	struct FFlightTelemetry
	{
		double AirspeedMps = 0.0;
		double AngleOfAttackRad = 0.0;
		double LiftCoeff = 0.0;
		bool bStalled = false;
		FVec3 LiftForce;
		FVec3 DragForce;
		FVec3 ThrustForce;
		FVec3 TotalForce;                     // excluding gravity
	};

	// Aerodynamic coefficient curve (exposed for tests and tuning tools).
	double ComputeLiftCoeff(const FFlightParams& Params, double AngleOfAttackRad);
	double GetStallAngleRad(const FFlightParams& Params);

	// Advance the simulation by DeltaSec using constant input and a constant wind velocity
	// (world frame, m/s). Internally substeps at Params.MaxSubstepSec. Deterministic: the same
	// state + input + wind + dt always gives the same result.
	FFlightTelemetry Step(FFlightState& State, const FFlightInput& Input, const FFlightParams& Params,
		const FVec3& Wind, double DeltaSec);
}
