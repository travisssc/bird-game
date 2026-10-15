// Free look (hold Alt) and head look.
#include "doctest/doctest.h"
#include "test_helpers.h"
#include "FlightCore/LookControl.h"

#include <cmath>
#include <limits>

using namespace BirdFlight;
using namespace TestUtil;

namespace
{
	constexpr double Deg = Pi / 180.0;

	void Look(FLookState& L, const FFlightState& Body, const FLookParams& P, double Seconds, double YawDegPerFrame,
		double PitchDegPerFrame, bool bFreeLook)
	{
		const int Frames = static_cast<int>(std::lround(Seconds / FrameDt));
		for (int i = 0; i < Frames; ++i)
		{
			FLookInput In;
			In.MouseYawDeg = YawDegPerFrame;
			In.MousePitchDeg = PitchDegPerFrame;
			In.bFreeLook = bFreeLook;
			UpdateLook(L, In, Body, P, FrameDt);
		}
	}
}

TEST_CASE("Normal flight: the mouse moves the camera and the flight aim together")
{
	const FLookParams P;
	const FFlightState Body = CruiseState(12.0, 100.0);
	FLookState L;
	Look(L, Body, P, 0.5, 1.0, 0.2, false);
	CHECK(L.Mode == ELookMode::Flying);
	CHECK(L.AimYawRad == doctest::Approx(30.0 * Deg));
	CHECK(L.AimPitchRad == doctest::Approx(6.0 * Deg));
	CHECK(L.CameraYawRad == L.AimYawRad);
	CHECK(L.CameraPitchRad == L.AimPitchRad);
	// Pitch is clamped.
	Look(L, Body, P, 2.0, 0.0, 5.0, false);
	CHECK(L.AimPitchRad == doctest::Approx(P.PitchMaxDeg * Deg));
}

TEST_CASE("Holding Alt freezes the flight aim; the bird holds its course while you look around")
{
	const FLookParams P;
	const FFlightParams FP;
	FFlightState Body = CruiseState(12.0, 300.0);
	FLookState L;
	FFlightInput Fly;
	Fly.bGlide = true;
	// Look 120 deg right and a bit down while gliding.
	for (int i = 0; i < 180; ++i)
	{
		FLookInput In;
		In.bFreeLook = true;
		In.MouseYawDeg = i < 60 ? 2.0 : 0.0;
		In.MousePitchDeg = i < 60 ? -0.3 : 0.0;
		UpdateLook(L, In, Body, P, FrameDt);
		Fly.AimDirection = L.GetAimDirection();
		Step(Body, Fly, FP, FVec3(), FrameDt);
	}
	CHECK(L.Mode == ELookMode::FreeLook);
	CHECK(L.AimYawRad == 0.0);
	CHECK(L.CameraYawRad == doctest::Approx(120.0 * Deg));
	CHECK(std::fabs(Body.Forward.Y) < 1e-6); // body never turned
	CHECK(std::fabs(Body.Velocity.Y) < 1e-6);
	// The head looks right, as far as the neck allows.
	CHECK(L.HeadYawRad == doctest::Approx(P.NeckYawLimitDeg * Deg).epsilon(0.02));
	CHECK(L.HeadPitchRad < 0.0);
}

TEST_CASE("Head follows the camera within neck limits, and doesn't flick across when looking behind")
{
	const FLookParams P;
	const FFlightState Body = CruiseState(12.0, 100.0);
	FLookState L;
	Look(L, Body, P, 0.5, 1.5, 0.0, true); // camera 45 deg right
	CHECK(L.HeadYawRad == doctest::Approx(45.0 * Deg).epsilon(0.01));

	// Keep turning right, past behind and around to 150 deg left (= 210 right).
	double MinHeadYaw = 1e9;
	double LastHead = L.HeadYawRad;
	double WorstStep = 0.0;
	for (int i = 0; i < 110; ++i)
	{
		FLookInput In;
		In.bFreeLook = true;
		In.MouseYawDeg = 1.5;
		UpdateLook(L, In, Body, P, FrameDt);
		MinHeadYaw = std::fmin(MinHeadYaw, L.HeadYawRad);
		WorstStep = std::fmax(WorstStep, std::fabs(L.HeadYawRad - LastHead));
		LastHead = L.HeadYawRad;
	}
	CHECK(L.CameraYawRad == doctest::Approx(-150.0 * Deg).epsilon(0.01));
	CHECK(L.HeadYawRad == doctest::Approx(P.NeckYawLimitDeg * Deg)); // stayed on the right side
	CHECK(MinHeadYaw > 0.0);
	CHECK(WorstStep <= P.HeadTurnRateDegPerSec * Deg * FrameDt + 1e-9);

	// Coming round into the left side's range, the head whips across (rate-limited).
	Look(L, Body, P, 0.6, 1.0, 0.0, true); // camera now at -114 deg, inside the left neck range
	Look(L, Body, P, 0.6, 0.0, 0.0, true);
	CHECK(L.HeadYawRad < 0.0);
}

TEST_CASE("Releasing Alt swings the camera back behind the bird, then normal flight resumes")
{
	const FLookParams P;
	const FFlightState Body = CruiseState(12.0, 100.0);
	FLookState L;
	Look(L, Body, P, 0.5, 3.0, 1.0, true); // look 90 right, 30 up
	REQUIRE(L.CameraYawRad == doctest::Approx(90.0 * Deg));

	double Time = 0.0;
	while (L.Mode != ELookMode::Flying && Time < 2.0)
	{
		Look(L, Body, P, FrameDt, 0.0, 0.0, false);
		Time += FrameDt;
	}
	CAPTURE(Time);
	CHECK(L.Mode == ELookMode::Flying);
	CHECK(Time < P.ReturnTimeSec * 1.5);
	CHECK(Time > P.ReturnTimeSec * 0.5); // a swing, not a snap
	CHECK(L.CameraYawRad == L.AimYawRad);
	CHECK(L.AimYawRad == 0.0);           // releasing Alt never turns the bird
}

TEST_CASE("The mouse steers during the swing back, and Alt can be re-pressed mid-swing")
{
	const FLookParams P;
	const FFlightState Body = CruiseState(12.0, 100.0);
	FLookState L;
	Look(L, Body, P, 0.5, 3.0, 0.0, true);
	Look(L, Body, P, 0.1, -1.0, 0.0, false); // released, steering left while returning
	CHECK(L.Mode == ELookMode::Returning);
	CHECK(L.AimYawRad == doctest::Approx(-6.0 * Deg));
	const double CameraMidSwing = L.CameraYawRad;
	Look(L, Body, P, FrameDt, 0.5, 0.0, true); // re-press: camera picks up where it was
	CHECK(L.Mode == ELookMode::FreeLook);
	CHECK(L.CameraYawRad == doctest::Approx(CameraMidSwing + 0.5 * Deg));
	CHECK(L.AimYawRad == doctest::Approx(-6.0 * Deg));
}

TEST_CASE("In normal flight the head leads into turns")
{
	const FLookParams P;
	const FFlightState Body = CruiseState(12.0, 100.0);
	FLookState L;
	Look(L, Body, P, FrameDt, 40.0, 0.0, false); // aim snaps 40 deg right; body hasn't turned yet
	Look(L, Body, P, 0.3, 0.0, 0.0, false);
	CHECK(L.HeadYawRad == doctest::Approx(40.0 * Deg * P.HeadLeadFraction).epsilon(0.01));
}

TEST_CASE("Look control ignores NaN input and repairs NaN state")
{
	const FLookParams P;
	const FFlightState Body = CruiseState(12.0, 100.0);
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	FLookState L;
	L.CameraYawRad = NaN;
	L.HeadPitchRad = NaN;
	FLookInput In;
	In.MouseYawDeg = NaN;
	In.MousePitchDeg = std::numeric_limits<double>::infinity();
	for (int i = 0; i < 10; ++i)
	{
		UpdateLook(L, In, Body, P, i % 2 ? NaN : FrameDt);
	}
	CHECK(std::isfinite(L.AimYawRad));
	CHECK(std::isfinite(L.AimPitchRad));
	CHECK(std::isfinite(L.CameraYawRad));
	CHECK(std::isfinite(L.HeadYawRad));
	CHECK(std::isfinite(L.HeadPitchRad));
	CHECK(L.GetAimDirection().Size() == doctest::Approx(1.0));
}
