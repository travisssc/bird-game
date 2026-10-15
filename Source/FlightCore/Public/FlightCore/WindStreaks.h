// Visible wind: short, translucent streaks drifting with the air around the viewer.
//
// Each streak is a little trail of points carried by the real wind field, so the player can read
// the air: streaks lie along the wind, are longer and denser when it's strong, vanish in calm
// air, curve upward over a building's windward face and tumble in its wake. Purely cosmetic
// (not part of the authoritative simulation), but seeded so tests and replays are repeatable.
//
// The Unreal side calls Update() once per frame with the camera position and draws each live
// streak as a thin ribbon through Points[0..NumPoints) (Points[0] is the head) with opacity
// Alpha, for example via a Niagara ribbon fed from arrays.
#pragma once

#include "FlightCore/FlightMath.h"
#include "FlightCore/WindField.h"

#include <cstdint>

namespace BirdFlight
{
	struct FWindStreakParams
	{
		int NumStreaks = 160;              // streak budget at full visibility (<= MaxStreaks)
		double SpawnRadiusM = 60.0;        // streaks live within this distance of the viewer
		double MinSpawnDistM = 6.0;        // never spawn right in the camera's face
		double ViewForwardBias = 0.6;      // 0 = spawn all around, 1 = mostly ahead of the camera
		double LifetimeSec = 2.5;          // randomized between 60% and 100% of this
		double FadeSec = 0.6;              // fade in and out over this long
		double MinVisibleWindMps = 1.5;    // calmer air than this shows nothing
		double FullVisibleWindMps = 8.0;   // all streaks, full opacity from this speed up
		double MaxAlpha = 0.22;            // translucent even at full strength
		double TrailSampleSec = 0.07;      // streak length = trail span * this * wind speed
		std::uint32_t Seed = 7;
	};

	struct FWindStreak
	{
		static constexpr int MaxPoints = 8;

		FVec3 Points[MaxPoints];           // [0] = head (newest)
		int NumPoints = 0;
		double Age = 0.0;
		double Lifetime = 0.0;
		double SinceSample = 0.0;
		double Alpha = 0.0;                // opacity to draw with, 0..MaxAlpha
		bool bAlive = false;
	};

	class FWindStreakSystem
	{
	public:
		static constexpr int MaxStreaks = 256;

		explicit FWindStreakSystem(std::uint32_t Seed = 7);

		// Advance by DeltaSec: move streaks with the wind at TimeSec, retire old or far ones and
		// spawn new ones around the viewer.
		void Update(const FWindParams& Wind, double TimeSec, const FVec3& ViewPosition, const FVec3& ViewForward,
			const FWindStreakParams& Params, double DeltaSec);

		const FWindStreak& GetStreak(int Index) const { return Streaks[Index]; }
		int CountAlive() const;

		// 0..1: how visible the wind is at this speed (0 in calm air).
		static double Visibility(const FWindStreakParams& Params, double WindSpeedMps);

	private:
		double Random01();
		// Returns false (and leaves the slot dead) if the wind at the chosen spot is too weak to show.
		bool Spawn(FWindStreak& Streak, const FWindParams& Wind, double TimeSec, const FVec3& ViewPosition,
			const FVec3& ViewForward, const FWindStreakParams& Params);

		FWindStreak Streaks[MaxStreaks];
		std::uint64_t RngState;
		double SpawnCarry = 0.0; // fractional spawn attempts carried between frames
	};
}
