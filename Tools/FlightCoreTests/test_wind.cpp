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
