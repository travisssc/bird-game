#include "FlightCore/WindStreaks.h"

#include <algorithm>
#include <cmath>

namespace BirdFlight
{
	namespace
	{
		double SmoothStep(double X)
		{
			const double T = Clamp(X, 0.0, 1.0);
			return T * T * (3.0 - 2.0 * T);
		}
	}

	FWindStreakSystem::FWindStreakSystem(std::uint32_t Seed)
		: RngState(0x853C49E6748FEA9Bull ^ (static_cast<std::uint64_t>(Seed) << 17))
	{
	}

	double FWindStreakSystem::Random01()
	{
		// SplitMix64.
		std::uint64_t Z = (RngState += 0x9E3779B97F4A7C15ull);
		Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
		Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
		Z ^= Z >> 31;
		return static_cast<double>(Z >> 11) * (1.0 / 9007199254740992.0);
	}

	double FWindStreakSystem::Visibility(const FWindStreakParams& Params, double WindSpeedMps)
	{
		const double Range = std::fmax(Params.FullVisibleWindMps - Params.MinVisibleWindMps, 1e-3);
		return SmoothStep((WindSpeedMps - Params.MinVisibleWindMps) / Range);
	}

	int FWindStreakSystem::CountAlive() const
	{
		int Count = 0;
		for (const FWindStreak& S : Streaks)
		{
			Count += S.bAlive ? 1 : 0;
		}
		return Count;
	}

	bool FWindStreakSystem::Spawn(FWindStreak& Streak, const FWindParams& Wind, double TimeSec,
		const FVec3& ViewPosition, const FVec3& ViewForward, const FWindStreakParams& Params)
	{
		// Random direction (uniform on the sphere), pulled toward the view direction, at a distance
		// uniform in volume between MinSpawnDistM and SpawnRadiusM.
		const double Z = 2.0 * Random01() - 1.0;
		const double Angle = 2.0 * Pi * Random01();
		const double R = std::sqrt(std::fmax(1.0 - Z * Z, 0.0));
		const FVec3 Random(R * std::cos(Angle), R * std::sin(Angle), Z);
		const FVec3 Dir = (Random + ViewForward.GetSafeNormal() * (2.0 * Clamp(Params.ViewForwardBias, 0.0, 1.0)))
			.GetSafeNormal(Random);
		const double MinD = Clamp(Params.MinSpawnDistM, 0.0, Params.SpawnRadiusM);
		const double U = Random01();
		const double MinCube = MinD * MinD * MinD;
		const double MaxCube = Params.SpawnRadiusM * Params.SpawnRadiusM * Params.SpawnRadiusM;
		const double Distance = std::cbrt(MinCube + (MaxCube - MinCube) * U);
		FVec3 Pos = ViewPosition + Dir * Distance;
		Pos.Z = std::fmax(Pos.Z, Wind.GroundZ + 1.0); // nothing to see underground
		const double Keep = Visibility(Params, SampleWind(Wind, Pos, TimeSec).Size());
		if (Random01() >= Keep)
		{
			return false;
		}

		Streak = FWindStreak();
		Streak.bAlive = true;
		Streak.Lifetime = Params.LifetimeSec * (0.6 + 0.4 * Random01());
		Streak.Age = 0.0;
		Streak.Points[0] = Pos;
		Streak.NumPoints = 1;
		return true;
	}

	void FWindStreakSystem::Update(const FWindParams& Wind, double TimeSec, const FVec3& ViewPosition,
		const FVec3& ViewForward, const FWindStreakParams& Params, double DeltaSec)
	{
		if (!(DeltaSec > 0.0) || !std::isfinite(DeltaSec) || !ViewPosition.IsFinite())
		{
			return;
		}
		const double Dt = std::fmin(DeltaSec, 0.1);
		const double KeepRadius = Params.SpawnRadiusM * 1.3;
		const double SampleSec = std::fmax(Params.TrailSampleSec, 1e-3);
		const double Fade = std::fmax(Params.FadeSec, 1e-3);

		for (FWindStreak& S : Streaks)
		{
			if (!S.bAlive)
			{
				continue;
			}
			S.Age += Dt;
			const FVec3 ToViewer = S.Points[0] - ViewPosition;
			if (S.Age >= S.Lifetime || ToViewer.SizeSquared() > KeepRadius * KeepRadius)
			{
				S.bAlive = false;
				S.Alpha = 0.0;
				continue;
			}

			// Midpoint (RK2) advection keeps streaks on their streamline around buildings.
			const FVec3 Head = S.Points[0];
			const FVec3 V1 = SampleWind(Wind, Head, TimeSec);
			const FVec3 V2 = SampleWind(Wind, Head + V1 * (0.5 * Dt), TimeSec + 0.5 * Dt);
			const FVec3 NewHead = Head + V2 * Dt;

			// The head moves every frame; a new trail point is laid down every TrailSampleSec.
			S.SinceSample += Dt;
			if (S.SinceSample >= SampleSec || S.NumPoints == 1)
			{
				S.SinceSample = S.NumPoints == 1 ? 0.0 : S.SinceSample - SampleSec;
				for (int i = (std::min)(S.NumPoints, FWindStreak::MaxPoints - 1); i > 0; --i)
				{
					S.Points[i] = S.Points[i - 1];
				}
				S.NumPoints = (std::min)(S.NumPoints + 1, FWindStreak::MaxPoints);
			}
			S.Points[0] = NewHead;

			// Opacity: fade in/out over the lifetime, with the local wind strength, and with
			// distance (gone at the edge of the spawn radius and right next to the camera).
			const double Life = SmoothStep(S.Age / Fade) * SmoothStep((S.Lifetime - S.Age) / Fade);
			const double Strength = Visibility(Params, V2.Size());
			const double Dist = (NewHead - ViewPosition).Size();
			const double FarFade = SmoothStep((Params.SpawnRadiusM - Dist) / (0.25 * Params.SpawnRadiusM));
			const double NearFade = SmoothStep(Dist / std::fmax(Params.MinSpawnDistM, 1e-3));
			S.Alpha = Params.MaxAlpha * Life * Strength * FarFade * NearFade;
		}

		// Spawn attempts at a steady rate that fills the budget over about one lifetime, so streaks
		// don't all appear (and expire) in lockstep. Each attempt picks a spot and keeps it with a
		// chance equal to how visible the wind is there: density follows the local wind, so a
		// sheltered viewer still sees the wind blowing past nearby, and calm air shows nothing.
		const int Budget = (std::max)(0, (std::min)(Params.NumStreaks, MaxStreaks));
		const int Attempts = static_cast<int>(std::ceil(Budget * Dt / std::fmax(Params.LifetimeSec * 0.6, 1e-3)));
		int Alive = CountAlive();
		int Slot = 0;
		for (int Attempt = 0; Attempt < Attempts && Alive < Budget; ++Attempt)
		{
			while (Slot < MaxStreaks && Streaks[Slot].bAlive)
			{
				++Slot;
			}
			if (Slot >= MaxStreaks)
			{
				break;
			}
			if (Spawn(Streaks[Slot], Wind, TimeSec, ViewPosition, ViewForward, Params))
			{
				++Alive;
			}
		}
	}
}
