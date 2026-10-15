#include "FlightCore/WindField.h"

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
	}

	FVec3 SampleWind(const FWindParams& Params, const FVec3& Position, double TimeSec)
	{
		FVec3 Wind = Params.BaseWind;

		if (Params.GustStrengthMps > 0.0 && Params.GustPeriodSec > 0.0 && Params.GustLengthM > 0.0)
		{
			// Gusts are mostly horizontal; vertical component is smaller, like real turbulence.
			const FVec3 Gust(
				GustSignal(Params.Seed, 0, Position, TimeSec, Params.GustPeriodSec, Params.GustLengthM),
				GustSignal(Params.Seed, 1, Position, TimeSec, Params.GustPeriodSec, Params.GustLengthM),
				0.4 * GustSignal(Params.Seed, 2, Position, TimeSec, Params.GustPeriodSec, Params.GustLengthM));
			Wind += Gust * Params.GustStrengthMps;
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
