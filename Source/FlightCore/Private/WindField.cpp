#include "FlightCore/WindField.h"

#include <algorithm>
#include <cmath>

namespace BirdFlight
{
	namespace
	{
		// Deterministic hash -> [0, 1). SplitMix64 finalizer.
		double Hash01(std::uint64_t X)
		{
			X += 0x9E3779B97F4A7C15ull;
			X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
			X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
			X = X ^ (X >> 31);
			return static_cast<double>(X >> 11) * (1.0 / 9007199254740992.0);
		}

		// Smooth pseudo-random signal in roughly [-1, 1]: three incommensurate sine waves whose
		// phases depend on time and on position (so gusts have spatial size and travel).
		double GustSignal(std::uint32_t Seed, int Axis, const FVec3& Pos, double Time, double Period, double Length)
		{
			double Sum = 0.0;
			const double Multipliers[3] = { 1.0, 1.731, 2.913 };
			const double Weights[3] = { 0.55, 0.30, 0.15 };
			for (int i = 0; i < 3; ++i)
			{
				const std::uint64_t Key = (static_cast<std::uint64_t>(Seed) << 8) ^ static_cast<std::uint64_t>(Axis * 3 + i);
				const double Phase = 2.0 * Pi * Hash01(Key);
				const double DirAngle = 2.0 * Pi * Hash01(Key ^ 0xABCDEFull);
				const double TimeFreq = 2.0 * Pi * Multipliers[i] / Period;
				const double SpaceFreq = 2.0 * Pi * Multipliers[i] / Length;
				const double Along = Pos.X * std::cos(DirAngle) + Pos.Y * std::sin(DirAngle);
				Sum += Weights[i] * std::sin(TimeFreq * Time + SpaceFreq * Along + Phase);
			}
			return Sum;
		}

		// What one building does to the wind at a point.
		struct FObstacleEffect
		{
			double HorizontalScale = 1.0; // multiplies the horizontal wind (stagnation, wake shelter)
			double CrossMps = 0.0;        // sideways spill around the building, across the wind
			double VerticalMps = 0.0;     // updraft (+) or lee sink (-)
			double TurbulenceMps = 0.0;   // amplitude of fast, small-scale wake turbulence
		};

		// Smooth, cheap model of flow around a box (no CFD): air slows as it nears the windward
		// face and escapes up it and over the roof edge (ridge lift, strongest near roof height
		// just upwind of the face) and out around the sides; behind the building is a sheltered,
		// turbulent wake with a little sink, fading over a few building heights. Wind direction/speed come from the
		// steady wind at that height; the obstacle is approximated by its footprint's extent
		// across and along the wind.
		FObstacleEffect ObstacleEffect(const FWindParams& Params, const FWindObstacle& Ob, const FVec3& Pos,
			double WindDirX, double WindDirY, double WindSpeed)
		{
			FObstacleEffect E;
			const double H = Ob.Height;
			const double Top = Ob.BaseZ + H;
			const double Dx = Pos.X - Ob.CenterX;
			const double Dy = Pos.Y - Ob.CenterY;
			const double Reach = std::sqrt(Ob.HalfLengthX * Ob.HalfLengthX + Ob.HalfWidthY * Ob.HalfWidthY)
				+ (Params.WakeLengthHeights + 2.0) * H;
			if (H <= 0.0 || Pos.Z > Top + 2.0 * H || Pos.Z < Ob.BaseZ || Dx * Dx + Dy * Dy > Reach * Reach)
			{
				return E;
			}

			// Coordinates in the wind frame: A along the wind (+ = downwind), C across it.
			const double A = Dx * WindDirX + Dy * WindDirY;
			const double C = -Dx * WindDirY + Dy * WindDirX;
			const double Rel = Ob.YawRad - std::atan2(WindDirY, WindDirX);
			const double ExtentA = std::fabs(Ob.HalfLengthX * std::cos(Rel)) + std::fabs(Ob.HalfWidthY * std::sin(Rel));
			const double ExtentC = std::fabs(Ob.HalfLengthX * std::sin(Rel)) + std::fabs(Ob.HalfWidthY * std::cos(Rel));
			const double ZRel = (Pos.Z - Ob.BaseZ) / H; // 0 at the ground, 1 at the roof

			const double OutsideC = (std::max)(std::fabs(C) - ExtentC, 0.0) / (0.5 * H);
			const double CrossFactor = std::exp(-OutsideC * OutsideC);
			if (CrossFactor < 1e-4)
			{
				return E;
			}
			if (std::fabs(A) < ExtentA && OutsideC == 0.0 && ZRel < 1.0)
			{
				E.HorizontalScale = 0.0; // inside the building: collision's problem, keep it calm
				return E;
			}

			// Stagnation: below roof height the air slows nearly to a stop against the windward face
			// and escapes up (the updraft below) and sideways, most strongly near the edges.
			const double Upwind = -ExtentA - A; // > 0 upwind of the windward face
			const double StagAlong = Upwind >= 0.0 ? std::exp(-Upwind / (0.4 * H)) : std::exp(Upwind / (0.3 * H));
			const double AboveRoofEdge = (ZRel - 1.0) / 0.15;
			const double BelowRoof = ZRel <= 1.0 ? 1.0 : std::exp(-AboveRoofEdge * AboveRoofEdge);
			E.HorizontalScale = 1.0 - 0.85 * StagAlong * BelowRoof * CrossFactor;
			const double Edge = Clamp(std::fabs(C) / (std::max)(ExtentC, 1e-3), 0.0, 1.0);
			E.CrossMps = (C >= 0.0 ? 1.0 : -1.0) * Params.SideSpill * WindSpeed * StagAlong * BelowRoof * CrossFactor * Edge;

			// Updraft, windward side. Upwind of the face it grows toward the face and with height up
			// to the roof; it carries on over the roof (and around the sides) near the leading edge
			// and dies off above.
			const double AlongFactor = Upwind >= 0.0 ? std::exp(-Upwind / (0.7 * H)) : std::exp(Upwind / (0.3 * H));
			const double AboveRoof = (ZRel - 1.0) / 0.5;
			const double LiftHeightFactor = ZRel <= 1.0 ? ZRel : std::exp(-AboveRoof * AboveRoof);
			E.VerticalMps = Params.RidgeLiftScale * WindSpeed * AlongFactor * LiftHeightFactor * CrossFactor;

			// Wake, lee side: shelter builds up alongside the building, is strongest right behind it
			// and fades over WakeLengthHeights. Everything is continuous so flying through the edges
			// of a wake never kicks the bird.
			const double FromFace = A + ExtentA; // > 0 downwind of the windward face
			if (FromFace > 0.0)
			{
				const double Ramp = Clamp(FromFace / (std::max)(2.0 * ExtentA, 1e-3), 0.0, 1.0);
				const double RampIn = Ramp * Ramp * (3.0 - 2.0 * Ramp);
				const double Downwind = (std::max)(A - ExtentA, 0.0); // behind the lee face
				const double WakeLength = (std::max)(Params.WakeLengthHeights, 0.1) * H;
				const double Fade = Clamp(1.0 - Downwind / WakeLength, 0.0, 1.0);
				const double AboveWake = (ZRel - 1.0) / 0.3;
				const double WakeHeight = ZRel <= 1.0 ? 1.0 : std::exp(-AboveWake * AboveWake);
				const double Wake = RampIn * Fade * Fade * WakeHeight * CrossFactor;
				E.HorizontalScale = (std::min)(E.HorizontalScale, 1.0 - Params.WakeCalm * Wake);
				E.TurbulenceMps = Params.WakeTurbulence * WindSpeed * Wake;
				// Sink peaks about one building height behind the lee face.
				const double SinkProfile = (Downwind / H) * std::exp(1.0 - Downwind / H);
				E.VerticalMps -= Params.WakeSink * WindSpeed * SinkProfile * Fade * WakeHeight * CrossFactor;
			}
			return E;
		}
	}

	double GetShearFactor(const FWindParams& Params, double Z)
	{
		if (Params.ShearExponent == 0.0)
		{
			return 1.0;
		}
		const double Height = Z - Params.GroundZ;
		const double Ratio = Height / (std::max)(Params.ShearRefHeightM, 1e-3);
		const double Factor = Ratio > 0.0 ? std::pow(Ratio, Params.ShearExponent) : 0.0;
		return Clamp(Factor, Params.ShearMinFactor, (std::max)(Params.ShearMaxFactor, Params.ShearMinFactor));
	}

	FVec3 SampleWind(const FWindParams& Params, const FVec3& Position, double TimeSec)
	{
		const double Shear = GetShearFactor(Params, Position.Z);
		FVec3 Wind = Params.BaseWind * Shear;

		if (Params.GustStrengthMps > 0.0 && Params.GustPeriodSec > 0.0 && Params.GustLengthM > 0.0)
		{
			// Gusts are mostly horizontal; vertical component is smaller, like real turbulence.
			const FVec3 Gust(
				GustSignal(Params.Seed, 0, Position, TimeSec, Params.GustPeriodSec, Params.GustLengthM),
				GustSignal(Params.Seed, 1, Position, TimeSec, Params.GustPeriodSec, Params.GustLengthM),
				0.4 * GustSignal(Params.Seed, 2, Position, TimeSec, Params.GustPeriodSec, Params.GustLengthM));
			Wind += Gust * (Params.GustStrengthMps * Shear);
		}

		if (Params.Obstacles && Params.NumObstacles > 0)
		{
			const double BaseX = Params.BaseWind.X * Shear;
			const double BaseY = Params.BaseWind.Y * Shear;
			const double Speed = std::sqrt(BaseX * BaseX + BaseY * BaseY);
			if (Speed > 1e-3)
			{
				const double DirX = BaseX / Speed;
				const double DirY = BaseY / Speed;
				double HorizontalScale = 1.0;
				double Cross = 0.0;
				double Vertical = 0.0;
				double Turbulence = 0.0;
				for (int i = 0; i < Params.NumObstacles; ++i)
				{
					const FObstacleEffect E = ObstacleEffect(Params, Params.Obstacles[i], Position, DirX, DirY, Speed);
					HorizontalScale = (std::min)(HorizontalScale, E.HorizontalScale);
					Cross += E.CrossMps;
					Vertical += E.VerticalMps;
					Turbulence = (std::max)(Turbulence, E.TurbulenceMps);
				}
				Wind.X = Wind.X * HorizontalScale - DirY * Cross;
				Wind.Y = Wind.Y * HorizontalScale + DirX * Cross;
				Wind.Z += Vertical;
				if (Turbulence > 0.0)
				{
					// Fast, small eddies: about a second and a few meters across.
					const std::uint32_t TurbSeed = Params.Seed ^ 0x7B3Du;
					Wind += FVec3(GustSignal(TurbSeed, 0, Position, TimeSec, 0.9, 7.0),
						GustSignal(TurbSeed, 1, Position, TimeSec, 0.9, 7.0),
						GustSignal(TurbSeed, 2, Position, TimeSec, 0.9, 7.0)) * Turbulence;
				}
			}
		}

		for (int i = 0; i < Params.NumThermals && i < FWindParams::MaxThermals; ++i)
		{
			const FThermal& Th = Params.Thermals[i];
			if (Th.Radius <= 0.0 || Position.Z >= Th.TopZ)
			{
				continue;
			}
			const double Dx = Position.X - Th.CenterX;
			const double Dy = Position.Y - Th.CenterY;
			const double RadialFactor = std::exp(-(Dx * Dx + Dy * Dy) / (Th.Radius * Th.Radius));
			const double FadeStart = Th.TopZ * 0.8;
			const double HeightFactor = Position.Z <= FadeStart ? 1.0 : (Th.TopZ - Position.Z) / (Th.TopZ - FadeStart);
			Wind.Z += Th.CoreUpdraftMps * RadialFactor * HeightFactor;
		}

		return Wind;
	}
}
