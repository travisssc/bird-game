#include "doctest/doctest.h"
#include "FlightCore/FlightMath.h"

#include <cmath>
#include <limits>

using namespace BirdFlight;

TEST_CASE("Cross product follows Unreal axes (X fwd, Y right, Z up)")
{
	const FVec3 Fwd(1, 0, 0), Right(0, 1, 0), Up(0, 0, 1);
	const FVec3 A = Cross(Fwd, Right);
	const FVec3 B = Cross(Up, Fwd);
	CHECK(A.Z == doctest::Approx(1.0));
	CHECK(B.Y == doctest::Approx(1.0));
}

TEST_CASE("GetSafeNormal returns the fallback for zero and non-finite vectors")
{
	const FVec3 Fallback(0, 0, 1);
	CHECK(FVec3().GetSafeNormal(Fallback).Z == 1.0);
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	CHECK(FVec3(NaN, 0, 0).GetSafeNormal(Fallback).Z == 1.0);
	CHECK(FVec3(3, 4, 0).GetSafeNormal().X == doctest::Approx(0.6));
}

TEST_CASE("RotateToward limits the step and handles opposite vectors")
{
	const FVec3 From(1, 0, 0);
	const FVec3 To(0, 1, 0);
	const FVec3 Step = RotateToward(From, To, Pi / 4.0, FVec3(0, 0, 1));
	CHECK(std::acos(Dot(Step, From)) == doctest::Approx(Pi / 4.0));
	CHECK(Step.Size() == doctest::Approx(1.0));
	CHECK(Step.Y > 0.0);

	// Small remaining angle snaps exactly to the target.
	const FVec3 Snap = RotateToward(From, To, Pi, FVec3(0, 0, 1));
	CHECK(Snap.Y == doctest::Approx(1.0));

	// Exactly opposite: rotates around the fallback axis instead of producing NaN.
	const FVec3 Opp = RotateToward(From, FVec3(-1, 0, 0), 0.1, FVec3(0, 0, 1));
	CHECK(Opp.IsFinite());
	CHECK(std::acos(Dot(Opp, From)) == doctest::Approx(0.1));
}

TEST_CASE("MoveToward clamps the step and lands exactly on the target")
{
	CHECK(MoveToward(0.0, 1.0, 0.25) == doctest::Approx(0.25));
	CHECK(MoveToward(0.0, -1.0, 0.25) == doctest::Approx(-0.25));
	CHECK(MoveToward(0.9, 1.0, 0.25) == 1.0);
}
