// Shared helpers for flight-core tests.
#pragma once

#include "FlightCore/FlightModel.h"
#include "FlightCore/WindField.h"

#include <cmath>
#include <functional>

namespace TestUtil
{
	using namespace BirdFlight;

	constexpr double FrameDt = 1.0 / 60.0;

	inline FVec3 DirFromPitchYawDeg(double PitchDeg, double YawDeg)
	{
		const double P = PitchDeg * Pi / 180.0;
		const double Y = YawDeg * Pi / 180.0;
		return FVec3(std::cos(P) * std::cos(Y), std::cos(P) * std::sin(Y), std::sin(P));
	}

	inline double MechanicalEnergy(const FFlightState& S, const FFlightParams& P)
	{
		return 0.5 * P.MassKg * S.Velocity.SizeSquared() + P.MassKg * P.Gravity * S.Position.Z;
	}

	inline bool IsFiniteState(const FFlightState& S)
	{
		return S.Position.IsFinite() && S.Velocity.IsFinite() && S.Forward.IsFinite() && S.Up.IsFinite()
			&& std::isfinite(S.BankRad) && std::isfinite(S.WingExtension) && std::isfinite(S.FlapPhase);
	}

	// Run for Seconds at 60 fps. PerFrame (optional) is called after each frame with the time
	// and telemetry, and may modify the input for the next frame.
	using FFrameFn = std::function<void(double, const FFlightTelemetry&, FFlightState&, FFlightInput&)>;

	inline FFlightTelemetry Simulate(FFlightState& State, FFlightInput Input, const FFlightParams& Params,
		double Seconds, const FWindParams* Wind = nullptr, const FFrameFn& PerFrame = nullptr)
	{
		FFlightTelemetry T;
		const int Frames = static_cast<int>(std::lround(Seconds / FrameDt));
		for (int i = 0; i < Frames; ++i)
		{
			const double Time = i * FrameDt;
			const FVec3 W = Wind ? SampleWind(*Wind, State.Position, Time) : FVec3();
			T = Step(State, Input, Params, W, FrameDt);
			if (PerFrame)
			{
				PerFrame(Time + FrameDt, T, State, Input);
			}
		}
		return T;
	}

	// A bird in level flight at the given speed and altitude, wings spread.
	inline FFlightState CruiseState(double Speed, double Altitude)
	{
		FFlightState S;
		S.Position = FVec3(0.0, 0.0, Altitude);
		S.Velocity = FVec3(Speed, 0.0, 0.0);
		S.WingExtension = 1.0;
		return S;
	}
}
