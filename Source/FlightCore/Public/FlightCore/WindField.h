// Deterministic wind field: steady wind that grows with height + smooth gusts + thermals
// (rising air columns) + the flow around buildings (updraft on the windward face, a calm,
// turbulent, sinking wake on the lee side).
//
// Deterministic so every client and the server compute the same wind for the same
// (position, time, seed): no wind state needs to be replicated beyond the parameters.
#pragma once

#include "FlightCore/FlightMath.h"

#include <cstdint>

namespace BirdFlight
{
	struct FThermal
	{
		double CenterX = 0.0;        // m (world X/Y; thermals are vertical columns)
		double CenterY = 0.0;
		double Radius = 30.0;        // m, the core updraft falls off as a Gaussian of this radius
		double CoreUpdraftMps = 3.0; // vertical wind at the center
		double TopZ = 300.0;         // m, the updraft fades to zero over the top 20% of the column
	};

	// A building (or any big blunt obstacle) as an upright box. The Unreal side fills these from
	// the level; a blockout city is boxes anyway.
	struct FWindObstacle
	{
		double CenterX = 0.0;        // m, footprint center
		double CenterY = 0.0;
		double HalfLengthX = 10.0;   // m, footprint half extents in the box's own frame
		double HalfWidthY = 10.0;
		double YawRad = 0.0;         // footprint rotation about world up
		double BaseZ = 0.0;          // m, ground level under the box
		double Height = 30.0;        // m
	};

	struct FWindParams
	{
		static constexpr int MaxThermals = 16;

		FVec3 BaseWind;               // m/s, steady wind at ShearRefHeightM above GroundZ
		double GustStrengthMps = 0.0; // typical gust magnitude (0 = steady air)
		double GustPeriodSec = 4.0;   // typical time between gust peaks
		double GustLengthM = 60.0;    // typical spatial size of a gust
		std::uint32_t Seed = 1;

		// Wind shear: wind grows with height (power law), so streets are sheltered and rooftops
		// windy. Gusts scale with it. Exponent 0 = the same wind at every height.
		double GroundZ = 0.0;
		double ShearRefHeightM = 50.0;
		double ShearExponent = 0.0;   // ~0.3 over a city
		double ShearMinFactor = 0.15; // floor near the ground
		double ShearMaxFactor = 2.0;

		FThermal Thermals[MaxThermals];
		int NumThermals = 0;

		// Buildings. Non-owning: the caller keeps the array alive while sampling.
		const FWindObstacle* Obstacles = nullptr;
		int NumObstacles = 0;
		double RidgeLiftScale = 0.6;     // peak updraft at the windward face, as a fraction of wind speed
		double SideSpill = 0.6;          // sideways flow around the windward corners, fraction of wind speed
		double WakeLengthHeights = 4.0;  // lee wake length, in building heights
		double WakeCalm = 0.8;           // fraction of the horizontal wind removed right behind a building
		double WakeSink = 0.2;           // downdraft right behind a building, fraction of wind speed
		double WakeTurbulence = 0.35;    // turbulence amplitude in the wake, fraction of wind speed

		// A breezy city day: ~5 m/s at 50 m, calmer in the streets, light gusts.
		static FWindParams CityBreeze()
		{
			FWindParams W;
			W.BaseWind = FVec3(5.0, 2.0, 0.0);
			W.ShearExponent = 0.3;
			W.GustStrengthMps = 1.5;
			W.GustPeriodSec = 5.0;
			W.GustLengthM = 80.0;
			return W;
		}

		// Returns false if full.
		bool AddThermal(const FThermal& Thermal)
		{
			if (NumThermals >= MaxThermals)
			{
				return false;
			}
			Thermals[NumThermals++] = Thermal;
			return true;
		}
	};

	// Wind velocity (m/s, world frame) at a position and time.
	FVec3 SampleWind(const FWindParams& Params, const FVec3& Position, double TimeSec);

	// Multiplier on BaseWind at a height (the shear profile alone).
	double GetShearFactor(const FWindParams& Params, double Z);
}
