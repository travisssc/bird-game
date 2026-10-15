// Behavioral tests for the flight model. Each test states a design expectation in
// physical terms (glide ratio, dive speed, climb, turn), not a snapshot of current numbers.
#include "doctest/doctest.h"
#include "test_helpers.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace BirdFlight;
using namespace TestUtil;

TEST_CASE("Lift curve: linear before stall, peaks at stall, drops after, vanishes sideways")
{
	const FFlightParams P;
	const double Stall = GetStallAngleRad(P);
	CHECK(ComputeLiftCoeff(P, 0.0) == 0.0);
	CHECK(ComputeLiftCoeff(P, 0.1) == doctest::Approx(P.LiftSlopePerRad * 0.1));
	CHECK(ComputeLiftCoeff(P, Stall) == doctest::Approx(P.MaxLiftCoeff));
	CHECK(ComputeLiftCoeff(P, Stall + 0.2) < P.MaxLiftCoeff);
	CHECK(ComputeLiftCoeff(P, -0.1) == doctest::Approx(-ComputeLiftCoeff(P, 0.1)));
	CHECK(ComputeLiftCoeff(P, Pi / 2.0) == doctest::Approx(0.0));
	CHECK(ComputeLiftCoeff(P, 2.5) == 0.0);
	// Crow stall angle should be a plausible 12-22 degrees.
	CHECK(Stall * 180.0 / Pi == doctest::Approx(17.0).epsilon(0.3));
}

TEST_CASE("Aiming level while gliding gives a gentle descent with a crow-like glide ratio")
{
	// Like a real glider, nose attitude sets speed: a level nose settles into a shallow,
	// stable descending glide (the wing meets the air at a few degrees of angle of attack).
	const FFlightParams P;
	FFlightState S = CruiseState(11.0, 500.0);
	FFlightInput In;
	In.bGlide = true;
	In.AimDirection = FVec3(1.0, 0.0, 0.0);

	Simulate(S, In, P, 5.0); // settle into the glide
	const FVec3 Start = S.Position;
	bool SawStall = false;
	const FFlightTelemetry T = Simulate(S, In, P, 20.0, nullptr,
		[&](double, const FFlightTelemetry& Tel, FFlightState&, FFlightInput&) { SawStall |= Tel.bStalled; });

	const double Distance = S.Position.X - Start.X;
	const double HeightLost = Start.Z - S.Position.Z;
	REQUIRE(HeightLost > 0.0);
	const double GlideRatio = Distance / HeightLost;
	CAPTURE(GlideRatio);
	CAPTURE(T.AirspeedMps);
	CHECK(GlideRatio > 6.0);
	CHECK(GlideRatio < 14.0);
	CHECK_FALSE(SawStall);
	CHECK(T.AirspeedMps > 7.0);
	CHECK(T.AirspeedMps < 18.0);
}

TEST_CASE("Holding the nose high without flapping bleeds speed into a stall, then the nose drops")
{
	const FFlightParams P;
	FFlightState S = CruiseState(12.0, 500.0);
	FFlightInput In;
	In.bGlide = true;
	In.AimDirection = DirFromPitchYawDeg(25.0, 0.0);

	bool SawStall = false;
	double MinPitchWhileStalled = 1.0;
	Simulate(S, In, P, 15.0, nullptr, [&](double, const FFlightTelemetry& Tel, FFlightState& St, FFlightInput&) {
		if (Tel.bStalled)
		{
			SawStall = true;
			MinPitchWhileStalled = std::min(MinPitchWhileStalled, St.Forward.Z);
		}
	});
	CHECK(SawStall);
	CHECK(MinPitchWhileStalled < DirFromPitchYawDeg(15.0, 0.0).Z); // nose dropped well below the aim
	CHECK(IsFiniteState(S));
}

TEST_CASE("A tucked dive is much faster than a spread-wing dive, with a sane terminal speed")
{
	const FFlightParams P;
	auto DiveSpeed = [&](bool bTuck) {
		FFlightState S = CruiseState(10.0, 3000.0);
		FFlightInput In;
		In.bTuck = bTuck;
		In.bGlide = !bTuck;
		In.AimDirection = FVec3(0.0, 0.0, -1.0);
		return Simulate(S, In, P, 25.0).AirspeedMps;
	};
	const double Tucked = DiveSpeed(true);
	const double Spread = DiveSpeed(false);
	CAPTURE(Tucked);
	CAPTURE(Spread);
	CHECK(Tucked > 1.4 * Spread);
	CHECK(Tucked > 40.0);
	CHECK(Tucked < 90.0);
}

TEST_CASE("Diving then pulling up converts speed back into height")
{
	const FFlightParams P;
	FFlightState S = CruiseState(12.0, 300.0);
	FFlightInput In;
	In.bTuck = true;
	In.AimDirection = DirFromPitchYawDeg(-60.0, 0.0);
	Simulate(S, In, P, 4.0);
	const double BottomZ = S.Position.Z;
	const double BottomSpeed = S.Velocity.Size();

	In.bTuck = false;
	In.bGlide = true;
	In.AimDirection = DirFromPitchYawDeg(35.0, 0.0);
	double MaxZ = BottomZ;
	Simulate(S, In, P, 4.0, nullptr,
		[&](double, const FFlightTelemetry&, FFlightState& St, FFlightInput&) { MaxZ = std::max(MaxZ, St.Position.Z); });
	CAPTURE(BottomSpeed);
	CHECK(BottomSpeed > 25.0);
	CHECK(MaxZ - BottomZ > 10.0); // zoom climb
}

TEST_CASE("Flapping at cruise speed climbs")
{
	const FFlightParams P;
	FFlightState S = CruiseState(12.0, 100.0);
	FFlightInput In;
	In.bFlap = true;
	In.AimDirection = DirFromPitchYawDeg(5.0, 0.0);
	Simulate(S, In, P, 10.0);
	CHECK(S.Position.Z - 100.0 > 3.0);
}

TEST_CASE("Flapping from a standstill takes off forward")
{
	const FFlightParams P;
	FFlightState S;
	S.Position = FVec3(0.0, 0.0, 0.0);
	S.WingExtension = 0.5;
	FFlightInput In;
	In.bFlap = true;
	In.AimDirection = DirFromPitchYawDeg(30.0, 0.0);
	Simulate(S, In, P, 2.0);
	CHECK(S.Position.Z > 0.5);
	CHECK(S.Position.X > 0.5);
}

TEST_CASE("Gliding without flapping can't gain energy (no free lunch)")
{
	// Lift is perpendicular to airflow and every other aerodynamic force opposes motion,
	// so with no flapping and no wind, kinetic + potential energy must never increase.
	const FFlightParams P;
	FFlightState S = CruiseState(14.0, 800.0);
	FFlightInput In;
	In.bGlide = true;
	double LastEnergy = MechanicalEnergy(S, P);
	double WorstIncrease = 0.0;
	Simulate(S, In, P, 40.0, nullptr, [&](double Time, const FFlightTelemetry&, FFlightState& St, FFlightInput& Inp) {
		// Wander the aim around: climbs, dives, hard turns, tuck/spread changes.
		Inp.AimDirection = DirFromPitchYawDeg(25.0 * std::sin(Time * 0.7), 120.0 * std::sin(Time * 0.31));
		Inp.bTuck = std::fmod(Time, 7.0) > 5.0;
		Inp.bGlide = !Inp.bTuck;
		const double E = MechanicalEnergy(St, P);
		WorstIncrease = std::max(WorstIncrease, E - LastEnergy);
		LastEnergy = E;
	});
	CAPTURE(WorstIncrease);
	CHECK(WorstIncrease < 1e-3); // joules per frame; allows only integrator round-off
}

TEST_CASE("Turning: the nose follows the aim, the bird banks into the turn, velocity follows")
{
	const FFlightParams P;
	SUBCASE("right turn")
	{
		FFlightState S = CruiseState(12.0, 300.0);
		FFlightInput In;
		In.bFlap = true;
		In.AimDirection = FVec3(0.0, 1.0, 0.0); // +Y is right in Unreal
		Simulate(S, In, P, 0.3);
		CHECK(S.BankRad > 0.2); // right wing down
		Simulate(S, In, P, 3.0);
		CHECK(S.Forward.Y > 0.98);
		CHECK(S.Velocity.Y > 0.8 * std::hypot(S.Velocity.X, S.Velocity.Y));
		CHECK(std::fabs(S.BankRad) < 0.1); // rolls level once the turn is complete
	}
	SUBCASE("left turn banks the other way")
	{
		FFlightState S = CruiseState(12.0, 300.0);
		FFlightInput In;
		In.bFlap = true;
		In.AimDirection = FVec3(0.0, -1.0, 0.0);
		Simulate(S, In, P, 0.3);
		CHECK(S.BankRad < -0.2);
	}
	SUBCASE("tucked wings turn slower")
	{
		FFlightState A = CruiseState(12.0, 300.0);
		FFlightState B = CruiseState(12.0, 300.0);
		B.WingExtension = 0.0;
		FFlightInput InA;
		InA.bGlide = true;
		InA.AimDirection = FVec3(0.0, 1.0, 0.0);
		FFlightInput InB = InA;
		InB.bGlide = false;
		InB.bTuck = true;
		Simulate(A, InA, P, 0.3);
		Simulate(B, InB, P, 0.3);
		CHECK(A.Forward.Y > B.Forward.Y + 0.1);
	}
}

TEST_CASE("Wing pose changes at a limited rate, and flap overrides tuck")
{
	const FFlightParams P;
	FFlightState S = CruiseState(12.0, 300.0);
	S.WingExtension = 0.5;
	FFlightInput In;
	In.bTuck = true;
	Step(S, In, P, FVec3(), 0.05);
	CHECK(S.WingExtension == doctest::Approx(0.5 - P.ExtensionRatePerSec * 0.05));
	Step(S, In, P, FVec3(), 0.2);
	CHECK(S.WingExtension == 0.0);
	In.bFlap = true;
	Step(S, In, P, FVec3(), 0.25);
	CHECK(S.WingExtension == 1.0);
}

TEST_CASE("Flap phase advances only while flapping")
{
	const FFlightParams P;
	FFlightState S = CruiseState(12.0, 300.0);
	FFlightInput In;
	In.bGlide = true;
	Step(S, In, P, FVec3(), 0.1);
	CHECK(S.FlapPhase == 0.0);
	In.bFlap = true;
	Step(S, In, P, FVec3(), 0.1);
	CHECK(S.FlapPhase == doctest::Approx(P.FlapFrequencyHz * 0.1));
}

TEST_CASE("Steady wind shifts the ground track exactly by wind * time")
{
	// In a uniform wind the bird flies the same path relative to the air mass.
	const FFlightParams P;
	FFlightInput In;
	In.bGlide = true;
	In.AimDirection = DirFromPitchYawDeg(-6.0, 0.0);

	FWindParams Wind;
	Wind.BaseWind = FVec3(-5.0, 2.0, 0.0);

	FFlightState Calm = CruiseState(11.0, 500.0);
	FFlightState Windy = Calm;
	Windy.Velocity += Wind.BaseWind;
	Simulate(Calm, In, P, 20.0);
	Simulate(Windy, In, P, 20.0, &Wind);

	const FVec3 Drift = Windy.Position - Calm.Position;
	CHECK(Drift.X == doctest::Approx(-100.0).epsilon(0.01));
	CHECK(Drift.Y == doctest::Approx(40.0).epsilon(0.01));
	CHECK(Drift.Z == doctest::Approx(0.0).epsilon(0.01));
}

TEST_CASE("Circling in a thermal gains height; the same circle in still air loses it")
{
	const FFlightParams P;
	FWindParams Thermal;
	FThermal T;
	T.Radius = 60.0;
	T.CoreUpdraftMps = 3.5;
	T.TopZ = 1000.0;
	Thermal.AddThermal(T);
	FWindParams Still;

	auto Circle = [&](const FWindParams& Wind) {
		FFlightState S = CruiseState(11.0, 400.0);
		S.Position = FVec3(0.0, -25.0, 400.0);
		FFlightInput In;
		In.bGlide = true;
		// Steer toward a point ahead on a 25 m circle around the thermal center.
		Simulate(S, In, P, 30.0, &Wind, [&](double, const FFlightTelemetry&, FFlightState& St, FFlightInput& Inp) {
			const double Angle = std::atan2(St.Position.Y, St.Position.X) + 0.6;
			const FVec3 Target(25.0 * std::cos(Angle), 25.0 * std::sin(Angle), St.Position.Z);
			const FVec3 Flat = (Target - St.Position);
			Inp.AimDirection = (FVec3(Flat.X, Flat.Y, 0.0).GetSafeNormal() + FVec3(0.0, 0.0, -0.12)).GetSafeNormal();
		});
		return S.Position.Z - 400.0;
	};
	const double ClimbThermal = Circle(Thermal);
	const double ClimbStill = Circle(Still);
	CAPTURE(ClimbThermal);
	CAPTURE(ClimbStill);
	CHECK(ClimbStill < 0.0);
	CHECK(ClimbThermal > 0.0);
}

TEST_CASE("Deterministic: identical inputs give bit-identical results")
{
	const FFlightParams P;
	FWindParams Wind;
	Wind.GustStrengthMps = 3.0;
	Wind.BaseWind = FVec3(2.0, 1.0, 0.0);
	auto Run = [&]() {
		FFlightState S = CruiseState(12.0, 300.0);
		FFlightInput In;
		Simulate(S, In, P, 10.0, &Wind, [](double Time, const FFlightTelemetry&, FFlightState&, FFlightInput& Inp) {
			Inp.bFlap = std::fmod(Time, 3.0) < 1.0;
			Inp.bGlide = !Inp.bFlap;
			Inp.AimDirection = DirFromPitchYawDeg(10.0 * std::sin(Time), 40.0 * std::cos(Time * 0.5));
		});
		return S;
	};
	const FFlightState A = Run();
	const FFlightState B = Run();
	CHECK(A.Position.X == B.Position.X);
	CHECK(A.Position.Y == B.Position.Y);
	CHECK(A.Position.Z == B.Position.Z);
	CHECK(A.Velocity.X == B.Velocity.X);
}

TEST_CASE("Robust against degenerate inputs (never produces NaN)")
{
	const FFlightParams P;
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const FVec3 Aims[] = {
		FVec3(), FVec3(NaN, 0, 0), FVec3(-1, 0, 0), FVec3(0, 0, -1), FVec3(0, 0, 1), FVec3(1e-12, 0, 0) };
	const double Dts[] = { 0.0, -1.0, NaN, 1e-6, 1.0 / 60.0, 10.0 };
	for (const FVec3& Aim : Aims)
	{
		for (double Dt : Dts)
		{
			for (int Mode = 0; Mode < 4; ++Mode)
			{
				FFlightState S; // at rest
				FFlightInput In;
				In.AimDirection = Aim;
				In.bFlap = Mode == 1;
				In.bTuck = Mode == 2;
				In.bGlide = Mode == 3;
				for (int i = 0; i < 30; ++i)
				{
					Step(S, In, P, FVec3(), Dt);
				}
				CHECK(IsFiniteState(S));
				CHECK(S.Forward.Size() == doctest::Approx(1.0));
				CHECK(S.Up.Size() == doctest::Approx(1.0));
				CHECK(std::fabs(Dot(S.Forward, S.Up)) < 1e-6);
			}
		}
	}
}

TEST_CASE("Long frames are clamped so a hitch can't teleport the bird")
{
	const FFlightParams P;
	FFlightState S = CruiseState(12.0, 300.0);
	FFlightInput In;
	In.bGlide = true;
	Step(S, In, P, FVec3(), 5.0);
	CHECK(S.Position.X < 12.0 * P.MaxStepSec * 1.5);
}

TEST_CASE("Flicking the aim behind the bird turns it around instead of diving or looping")
{
	// Regression: great-circle rotation toward an aim behind and slightly below the bird
	// pitched the nose down through vertical (a split-S) and snapped the roll.
	const FFlightParams P;
	const double AimYaws[] = { 150.0, 170.0, 179.0, 180.0, -175.0 };
	for (double AimYaw : AimYaws)
	{
		CAPTURE(AimYaw);
		FFlightState S = CruiseState(12.0, 300.0);
		FFlightInput In;
		In.bGlide = true;
		In.AimDirection = DirFromPitchYawDeg(-10.0, AimYaw);
		double MinNoseZ = 1.0;
		double MaxUpChangeDeg = 0.0;
		FVec3 LastUp = S.Up;
		Simulate(S, In, P, 3.0, nullptr, [&](double, const FFlightTelemetry&, FFlightState& St, FFlightInput&) {
			MinNoseZ = std::min(MinNoseZ, St.Forward.Z);
			MaxUpChangeDeg = std::max(MaxUpChangeDeg, std::acos(Clamp(Dot(LastUp, St.Up), -1.0, 1.0)) * 180.0 / Pi);
			LastUp = St.Up;
		});
		CHECK(MinNoseZ > DirFromPitchYawDeg(-30.0, 0.0).Z); // never pitches below -30 deg
		CHECK(MaxUpChangeDeg < 10.0);                       // no roll snap within one frame
		CHECK(Dot(S.Forward, In.AimDirection) > 0.99);       // completed the turn
	}
}

TEST_CASE("Step repairs a corrupted orientation and ignores non-finite wind")
{
	const FFlightParams P;
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	SUBCASE("Forward == Up (degenerate basis)")
	{
		FFlightState S = CruiseState(12.0, 300.0);
		S.Forward = FVec3(0.0, 0.0, 1.0);
		S.Up = FVec3(0.0, 0.0, 1.0);
		FFlightInput In;
		In.bGlide = true;
		In.AimDirection = FVec3(0.0, 0.0, 1.0);
		Step(S, In, P, FVec3(), FrameDt);
		CHECK(IsFiniteState(S));
		CHECK(std::fabs(Dot(S.Forward, S.Up)) < 1e-6);
		CHECK(S.GetRight().Size() == doctest::Approx(1.0));
	}
	SUBCASE("NaN wind")
	{
		FFlightState S = CruiseState(12.0, 300.0);
		FFlightInput In;
		In.bGlide = true;
		Step(S, In, P, FVec3(NaN, 0.0, 0.0), FrameDt);
		CHECK(IsFiniteState(S));
	}
	SUBCASE("NaN velocity and orientation")
	{
		FFlightState S = CruiseState(12.0, 300.0);
		S.Velocity = FVec3(NaN, 1.0, 0.0);
		S.Forward = FVec3(NaN, 0.0, 0.0);
		S.BankRad = NaN;
		S.WingExtension = NaN;
		FFlightInput In;
		Step(S, In, P, FVec3(), FrameDt);
		CHECK(IsFiniteState(S));
	}
}

TEST_CASE("SanitizeParams makes zero or negative tunables safe")
{
	FFlightParams P;
	P.MaxSubstepSec = 0.0;
	P.StallFalloffRad = 0.0;
	P.MassKg = 0.0;
	P.LiftSlopePerRad = 0.0;
	P.FlapFrequencyHz = -1.0;
	SanitizeParams(P);
	FFlightState S = CruiseState(12.0, 300.0);
	FFlightInput In;
	In.bGlide = true;
	In.AimDirection = DirFromPitchYawDeg(GetStallAngleRad(P) * 180.0 / Pi, 0.0);
	for (int i = 0; i < 120; ++i)
	{
		Step(S, In, P, FVec3(), FrameDt);
	}
	CHECK(IsFiniteState(S));
}

TEST_CASE("UpdateVelocityAndOrientation + caller-side move matches Step (the CMC integration path)")
{
	const FFlightParams P;
	FFlightState A = CruiseState(12.0, 300.0);
	FFlightState B = A;
	FFlightInput In;
	In.bFlap = true;
	In.AimDirection = DirFromPitchYawDeg(10.0, 45.0);
	const double H = P.MaxSubstepSec;
	for (int i = 0; i < 240; ++i)
	{
		Step(A, In, P, FVec3(), H);
		UpdateVelocityAndOrientation(B, In, P, FVec3(), H);
		B.Position += B.Velocity * H; // what the CMC sweep does when nothing is hit
	}
	CHECK(A.Position.X == B.Position.X);
	CHECK(A.Position.Z == B.Position.Z);
	CHECK(A.Velocity.Y == B.Velocity.Y);
}
