#include "doctest/doctest.h"
#include "FlightCore/WindField.h"

#include <cmath>

using namespace BirdFlight;

TEST_CASE("Calm params give exactly the base wind")
{
	FWindParams W;
	W.BaseWind = FVec3(3.0, -1.0, 0.0);
	const FVec3 S = SampleWind(W, FVec3(10.0, 20.0, 30.0), 5.0);
	CHECK(S.X == 3.0);
	CHECK(S.Y == -1.0);
	CHECK(S.Z == 0.0);
}

TEST_CASE("Gusts vary over time and space, stay bounded, and depend on the seed")
{
	FWindParams W;
	W.GustStrengthMps = 4.0;
	double MinX = 1e9, MaxX = -1e9, MaxAbsZ = 0.0;
	for (int i = 0; i < 2000; ++i)
	{
		const double T = i * 0.05;
		const FVec3 S = SampleWind(W, FVec3(i * 0.7, 0.0, 50.0), T);
		MinX = std::fmin(MinX, S.X);
		MaxX = std::fmax(MaxX, S.X);
		MaxAbsZ = std::fmax(MaxAbsZ, std::fabs(S.Z));
	}
	CHECK(MaxX - MinX > 2.0);              // actually gusty
	CHECK(MaxX <= 4.0 + 1e-9);             // weights sum to 1
	CHECK(MinX >= -4.0 - 1e-9);
	CHECK(MaxAbsZ <= 0.4 * 4.0 + 1e-9);    // vertical gusts are weaker

	FWindParams Other = W;
	Other.Seed = 2;
	const FVec3 P(5.0, 5.0, 5.0);
	CHECK(SampleWind(W, P, 1.0).X != SampleWind(Other, P, 1.0).X);
	CHECK(SampleWind(W, P, 1.0).X == SampleWind(W, P, 1.0).X);
}

TEST_CASE("Thermal: strongest at the center, weak far away, gone above the top")
{
	FWindParams W;
	FThermal T;
	T.CenterX = 100.0;
	T.CenterY = 50.0;
	T.Radius = 30.0;
	T.CoreUpdraftMps = 3.0;
	T.TopZ = 200.0;
	REQUIRE(W.AddThermal(T));

	CHECK(SampleWind(W, FVec3(100.0, 50.0, 10.0), 0.0).Z == doctest::Approx(3.0));
	CHECK(SampleWind(W, FVec3(130.0, 50.0, 10.0), 0.0).Z == doctest::Approx(3.0 * std::exp(-1.0)));
	CHECK(SampleWind(W, FVec3(400.0, 50.0, 10.0), 0.0).Z < 1e-6);
	CHECK(SampleWind(W, FVec3(100.0, 50.0, 180.0), 0.0).Z == doctest::Approx(1.5)); // fading zone
	CHECK(SampleWind(W, FVec3(100.0, 50.0, 250.0), 0.0).Z == 0.0);
}

TEST_CASE("AddThermal refuses when full")
{
	FWindParams W;
	for (int i = 0; i < FWindParams::MaxThermals; ++i)
	{
		CHECK(W.AddThermal(FThermal()));
	}
	CHECK_FALSE(W.AddThermal(FThermal()));
	CHECK(W.NumThermals == FWindParams::MaxThermals);
}

TEST_CASE("Wind shear: calm in the streets, windy over the rooftops")
{
	FWindParams W;
	W.BaseWind = FVec3(6.0, 0.0, 0.0);
	W.ShearExponent = 0.3;
	W.ShearRefHeightM = 50.0;
	CHECK(SampleWind(W, FVec3(0.0, 0.0, 50.0), 0.0).X == doctest::Approx(6.0));
	CHECK(SampleWind(W, FVec3(0.0, 0.0, 5.0), 0.0).X < 0.6 * 6.0);
	CHECK(SampleWind(W, FVec3(0.0, 0.0, 150.0), 0.0).X > 1.3 * 6.0);
	CHECK(SampleWind(W, FVec3(0.0, 0.0, -3.0), 0.0).X == doctest::Approx(W.ShearMinFactor * 6.0));
	CHECK(SampleWind(W, FVec3(0.0, 0.0, 1e6), 0.0).X == doctest::Approx(W.ShearMaxFactor * 6.0));
	double Last = 0.0;
	for (double Z = 0.0; Z < 400.0; Z += 5.0)
	{
		const double X = SampleWind(W, FVec3(0.0, 0.0, Z), 0.0).X;
		CHECK(X >= Last);
		Last = X;
	}
}

namespace
{
	// A 40 m tall slab, 30 m deep along the wind and 80 m wide across it, at the origin.
	// Wind blows along +X, so the windward face is at X = -15 and the lee face at X = +15.
	FWindObstacle Slab()
	{
		FWindObstacle B;
		B.HalfLengthX = 15.0;
		B.HalfWidthY = 40.0;
		B.Height = 40.0;
		return B;
	}

	FWindParams WindPast(const FWindObstacle& B, double Speed)
	{
		FWindParams W;
		W.BaseWind = FVec3(Speed, 0.0, 0.0);
		W.Obstacles = &B;
		W.NumObstacles = 1;
		return W;
	}
}

TEST_CASE("Buildings: updraft on the windward face, sheltered sinking wake behind, freestream far away")
{
	const FWindObstacle B = Slab();
	const FWindParams W = WindPast(B, 8.0);

	const FVec3 Windward = SampleWind(W, FVec3(-20.0, 0.0, 40.0), 0.0);
	CHECK(Windward.Z > 0.4 * 8.0);             // strong ridge lift near roof height
	CHECK(SampleWind(W, FVec3(-20.0, 0.0, 5.0), 0.0).Z < Windward.Z * 0.3); // weak near the ground
	CHECK(SampleWind(W, FVec3(-20.0, 0.0, 120.0), 0.0).Z < 0.1);            // gone high above

	// Lee: average over time (the wake is turbulent).
	FVec3 LeeMean;
	double LeeSpread = 0.0;
	const int N = 400;
	for (int i = 0; i < N; ++i)
	{
		const FVec3 S = SampleWind(W, FVec3(55.0, 0.0, 25.0), i * 0.05);
		LeeMean += S / N;
		LeeSpread = std::fmax(LeeSpread, std::fabs(S.X - LeeMean.X));
	}
	CHECK(LeeMean.X < 0.6 * 8.0); // sheltered
	CHECK(LeeMean.Z < -0.5);       // sinking air behind the building
	CHECK(LeeSpread > 0.5);        // bumpy

	// Far away in every direction: undisturbed.
	const FVec3 Far[] = { FVec3(-300.0, 0.0, 30.0), FVec3(0.0, 200.0, 30.0), FVec3(400.0, 0.0, 30.0) };
	for (const FVec3& P : Far)
	{
		const FVec3 S = SampleWind(W, P, 0.0);
		CHECK(S.X == doctest::Approx(8.0));
		CHECK(std::fabs(S.Z) < 0.05);
	}

	// No wind, no building effect.
	const FWindParams Calm = WindPast(B, 0.0);
	CHECK(SampleWind(Calm, FVec3(-20.0, 0.0, 40.0), 0.0).Z == 0.0);
}

TEST_CASE("Building wind is continuous: flying through it never kicks the bird")
{
	// Sweep lines past and over the slab and around its corners; adjacent samples 0.25 m apart
	// must never jump. (Inside the building itself is excluded: collision owns that.)
	const FWindObstacle B = Slab();
	FWindParams W = WindPast(B, 8.0);
	W.WakeTurbulence = 0.0; // turbulence is smooth noise; test the mean flow
	auto Inside = [&](const FVec3& P) { return std::fabs(P.X) < 15.0 && std::fabs(P.Y) < 40.0 && P.Z < 40.0; };
	const double Ys[] = { 0.0, 39.0, 41.0, 50.0 };
	const double Zs[] = { 10.0, 39.0, 41.0, 60.0 };
	double WorstJump = 0.0;
	for (double Y : Ys)
	{
		for (double Z : Zs)
		{
			FVec3 Last = SampleWind(W, FVec3(-200.0, Y, Z), 0.0);
			for (double X = -200.0; X < 300.0; X += 0.25)
			{
				const FVec3 P(X, Y, Z);
				const FVec3 S = SampleWind(W, P, 0.0);
				if (!Inside(P) && !Inside(FVec3(X - 0.25, Y, Z)))
				{
					WorstJump = std::fmax(WorstJump, (S - Last).Size());
				}
				Last = S;
			}
		}
	}
	CAPTURE(WorstJump);
	CHECK(WorstJump < 0.25);
}

TEST_CASE("Rotated buildings: the same box gives the same flow relative to the wind")
{
	// A box rotated 90 deg with swapped extents is the same footprint.
	FWindObstacle A = Slab();
	FWindObstacle B = A;
	B.YawRad = Pi / 2.0;
	B.HalfLengthX = A.HalfWidthY;
	B.HalfWidthY = A.HalfLengthX;
	const FWindParams WA = WindPast(A, 7.0);
	const FWindParams WB = WindPast(B, 7.0);
	const FVec3 Points[] = { FVec3(-20.0, 5.0, 38.0), FVec3(60.0, -10.0, 20.0), FVec3(0.0, 45.0, 30.0) };
	for (const FVec3& P : Points)
	{
		CHECK(SampleWind(WA, P, 1.0).X == doctest::Approx(SampleWind(WB, P, 1.0).X).epsilon(1e-9));
		CHECK(SampleWind(WA, P, 1.0).Z == doctest::Approx(SampleWind(WB, P, 1.0).Z).epsilon(1e-9));
	}
}

TEST_CASE("CityBreeze preset: never dead calm, gentler in the streets")
{
	const FWindParams W = FWindParams::CityBreeze();
	for (int i = 0; i < 200; ++i)
	{
		const double T = i * 0.3;
		const FVec3 High = SampleWind(W, FVec3(i * 3.0, 0.0, 80.0), T);
		CHECK(std::hypot(High.X, High.Y) > 2.0);
	}
	CHECK(GetShearFactor(W, 8.0) < GetShearFactor(W, 80.0));
}
