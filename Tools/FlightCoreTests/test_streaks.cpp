// Visible wind streaks.
#include "doctest/doctest.h"
#include "FlightCore/WindStreaks.h"

#include <cmath>

using namespace BirdFlight;

namespace
{
	constexpr double Dt = 1.0 / 60.0;

	void Run(FWindStreakSystem& Sys, const FWindParams& Wind, const FWindStreakParams& P, double Seconds,
		const FVec3& View = FVec3(0.0, 0.0, 50.0), const FVec3& Forward = FVec3(1.0, 0.0, 0.0), double StartTime = 0.0)
	{
		const int Frames = static_cast<int>(std::lround(Seconds / Dt));
		for (int i = 0; i < Frames; ++i)
		{
			Sys.Update(Wind, StartTime + i * Dt, View, Forward, P, Dt);
		}
	}

	double MaxAlpha(const FWindStreakSystem& Sys)
	{
		double A = 0.0;
		for (int i = 0; i < FWindStreakSystem::MaxStreaks; ++i)
		{
			A = std::fmax(A, Sys.GetStreak(i).Alpha);
		}
		return A;
	}
}

TEST_CASE("Calm air shows no streaks; wind brings them in, and calm again fades them out")
{
	const FWindStreakParams P;
	FWindStreakSystem Sys;
	FWindParams Calm;
	Run(Sys, Calm, P, 3.0);
	CHECK(Sys.CountAlive() == 0);

	FWindParams Breeze;
	Breeze.BaseWind = FVec3(9.0, 0.0, 0.0);
	Run(Sys, Breeze, P, 4.0);
	CHECK(Sys.CountAlive() > P.NumStreaks * 3 / 4);
	CHECK(MaxAlpha(Sys) > 0.5 * P.MaxAlpha);
	CHECK(MaxAlpha(Sys) <= P.MaxAlpha);

	Run(Sys, Calm, P, 0.1);
	CHECK(MaxAlpha(Sys) == 0.0); // invisible at once in calm air
	Run(Sys, Calm, P, 3.0);
	CHECK(Sys.CountAlive() == 0);
}

TEST_CASE("Stronger wind: more streaks, more opaque, longer")
{
	const FWindStreakParams P;
	auto Measure = [&](double Speed, int* OutAlive, double* OutLength) {
		FWindStreakSystem Sys;
		FWindParams W;
		W.BaseWind = FVec3(Speed, 0.0, 0.0);
		Run(Sys, W, P, 4.0);
		*OutAlive = Sys.CountAlive();
		double LongestFull = 0.0;
		for (int i = 0; i < FWindStreakSystem::MaxStreaks; ++i)
		{
			const FWindStreak& S = Sys.GetStreak(i);
			if (S.bAlive && S.NumPoints == FWindStreak::MaxPoints)
			{
				LongestFull = std::fmax(LongestFull, (S.Points[0] - S.Points[S.NumPoints - 1]).Size());
			}
		}
		*OutLength = LongestFull;
		return MaxAlpha(Sys);
	};
	int AliveLight = 0, AliveStrong = 0;
	double LengthLight = 0.0, LengthStrong = 0.0;
	const double AlphaLight = Measure(3.0, &AliveLight, &LengthLight);
	const double AlphaStrong = Measure(10.0, &AliveStrong, &LengthStrong);
	CHECK(AliveStrong > AliveLight);
	CHECK(AlphaStrong > AlphaLight);
	CHECK(LengthStrong > 2.5 * LengthLight);
	// Trail spans (MaxPoints - 1) samples of the wind.
	CHECK(LengthStrong == doctest::Approx(10.0 * P.TrailSampleSec * (FWindStreak::MaxPoints - 1)).epsilon(0.2));
}

TEST_CASE("Streaks lie along the wind and spawn around the viewer, mostly ahead")
{
	const FWindStreakParams P;
	FWindStreakSystem Sys;
	FWindParams W;
	W.BaseWind = FVec3(0.0, 7.0, 0.0); // blowing toward +Y
	const FVec3 View(100.0, -40.0, 60.0);
	const FVec3 Forward(1.0, 0.0, 0.0);
	Run(Sys, W, P, 3.0, View, Forward);
	int Count = 0;
	double AheadSum = 0.0;
	for (int i = 0; i < FWindStreakSystem::MaxStreaks; ++i)
	{
		const FWindStreak& S = Sys.GetStreak(i);
		if (!S.bAlive || S.NumPoints < 2)
		{
			continue;
		}
		const FVec3 Along = (S.Points[0] - S.Points[S.NumPoints - 1]).GetSafeNormal();
		CHECK(Along.Y > 0.999);
		const FVec3 Offset = S.Points[0] - View;
		CHECK(Offset.Size() < P.SpawnRadiusM * 1.3);
		AheadSum += Dot(Offset.GetSafeNormal(), Forward);
		++Count;
	}
	REQUIRE(Count > 50);
	CHECK(AheadSum / Count > 0.3);
}

TEST_CASE("Streaks show the updraft: they curve up the windward face of a building")
{
	FWindObstacle B;
	B.HalfLengthX = 15.0;
	B.HalfWidthY = 40.0;
	B.Height = 40.0;
	FWindParams W;
	W.BaseWind = FVec3(8.0, 0.0, 0.0);
	W.Obstacles = &B;
	W.NumObstacles = 1;
	FWindStreakParams P;
	FWindStreakSystem Sys;
	const FVec3 View(-35.0, 0.0, 35.0); // just upwind of the face
	Run(Sys, W, P, 3.0, View, FVec3(1.0, 0.0, 0.0));
	int Rising = 0, Counted = 0;
	for (int i = 0; i < FWindStreakSystem::MaxStreaks; ++i)
	{
		const FWindStreak& S = Sys.GetStreak(i);
		const FVec3& H = S.Points[0];
		if (S.bAlive && S.NumPoints > 3 && H.X > -30.0 && H.X < -16.0 && H.Z > 20.0 && H.Z < 45.0 && std::fabs(H.Y) < 30.0)
		{
			++Counted;
			Rising += (H.Z - S.Points[S.NumPoints - 1].Z) > 0.5 ? 1 : 0;
		}
	}
	REQUIRE(Counted > 3);
	CHECK(Rising == Counted);
}

TEST_CASE("Flying away recycles streaks around the new viewer position")
{
	const FWindStreakParams P;
	FWindStreakSystem Sys;
	FWindParams W;
	W.BaseWind = FVec3(8.0, 0.0, 0.0);
	Run(Sys, W, P, 3.0, FVec3(0.0, 0.0, 50.0));
	Run(Sys, W, P, 4.0, FVec3(1000.0, 0.0, 50.0), FVec3(1.0, 0.0, 0.0), 3.0);
	int Near = 0;
	for (int i = 0; i < FWindStreakSystem::MaxStreaks; ++i)
	{
		const FWindStreak& S = Sys.GetStreak(i);
		if (S.bAlive)
		{
			CHECK((S.Points[0] - FVec3(1000.0, 0.0, 50.0)).Size() < P.SpawnRadiusM * 1.3);
			++Near;
		}
	}
	CHECK(Near > P.NumStreaks / 2);
}

TEST_CASE("Streaks are repeatable for a seed and ignore bad frame times")
{
	const FWindStreakParams P;
	FWindParams W = FWindParams::CityBreeze();
	FWindStreakSystem A(3), B(3);
	Run(A, W, P, 2.0);
	Run(B, W, P, 2.0);
	for (int i = 0; i < FWindStreakSystem::MaxStreaks; ++i)
	{
		CHECK(A.GetStreak(i).Points[0].X == B.GetStreak(i).Points[0].X);
	}
	const int Before = A.CountAlive();
	A.Update(W, 2.0, FVec3(0.0, 0.0, 50.0), FVec3(1.0, 0.0, 0.0), P, -1.0);
	A.Update(W, 2.0, FVec3(0.0, 0.0, 50.0), FVec3(1.0, 0.0, 0.0), P, std::nan(""));
	CHECK(A.CountAlive() == Before);
}
