// Deterministic wind field: steady wind + smooth gusts + thermals (rising air columns).
//
// Deterministic so every client and the server compute the same wind for the same
// (position, time, seed): no wind state needs to be replicated beyond the parameters.
// Building updrafts (ridge lift) need level geometry and will live on the Unreal side.
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

	struct FWindParams
	{
		static constexpr int MaxThermals = 16;

		FVec3 BaseWind;               // m/s, steady wind
		double GustStrengthMps = 0.0; // typical gust magnitude (0 = steady air)
		double GustPeriodSec = 4.0;   // typical time between gust peaks
		double GustLengthM = 60.0;    // typical spatial size of a gust
		std::uint32_t Seed = 1;

		FThermal Thermals[MaxThermals];
		int NumThermals = 0;

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
}
