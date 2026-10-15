// Flight lab: runs scripted flight scenarios through the core and prints one CSV to stdout.
// Used to chart and sanity-check the model without the game (Tools/FlightCoreTests/plot_lab.py).
//   ./flight_lab > lab.csv
#include "FlightCore/FlightModel.h"
#include "FlightCore/WindField.h"

#include <cmath>
#include <cstdio>

using namespace BirdFlight;

namespace
{
	constexpr double Dt = 1.0 / 60.0;

	FVec3 Dir(double PitchDeg, double YawDeg)
	{
		const double P = PitchDeg * Pi / 180.0;
		const double Y = YawDeg * Pi / 180.0;
		return FVec3(std::cos(P) * std::cos(Y), std::cos(P) * std::sin(Y), std::sin(P));
	}

	void Row(const char* Name, double Time, const FFlightState& S, const FFlightTelemetry& T)
	{
		std::printf("%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%d,%.2f,%.1f\n", Name, Time, S.Position.X, S.Position.Y,
			S.Position.Z, T.AirspeedMps, T.AngleOfAttackRad * 180.0 / Pi, T.bStalled ? 1 : 0, S.WingExtension,
			S.BankRad * 180.0 / Pi);
	}

	// Phase = (duration, input). Runs phases back to back.
	struct FPhase
	{
		double Seconds;
		FFlightInput Input;
	};

	FFlightInput Make(FVec3 Aim, bool bGlide, bool bFlap, bool bTuck)
	{
		FFlightInput In;
		In.AimDirection = Aim;
		In.bGlide = bGlide;
		In.bFlap = bFlap;
		In.bTuck = bTuck;
		return In;
	}

	void Run(const char* Name, FFlightState S, const FPhase* Phases, int NumPhases, const FWindParams* Wind = nullptr)
	{
		const FFlightParams P;
		double Time = 0.0;
		for (int p = 0; p < NumPhases; ++p)
		{
			const int Frames = static_cast<int>(std::lround(Phases[p].Seconds / Dt));
			for (int i = 0; i < Frames; ++i)
			{
				const FVec3 W = Wind ? SampleWind(*Wind, S.Position, Time) : FVec3();
				const FFlightTelemetry T = Step(S, Phases[p].Input, P, W, Dt);
				Time += Dt;
				if (i % 6 == 0)
				{
					Row(Name, Time, S, T);
				}
			}
		}
	}

	FFlightState Cruise(double Speed, double Z)
	{
		FFlightState S;
		S.Position = FVec3(0.0, 0.0, Z);
		S.Velocity = FVec3(Speed, 0.0, 0.0);
		S.WingExtension = 1.0;
		return S;
	}
}

int main()
{
	std::printf("scenario,t,x,y,z,airspeed,aoa_deg,stalled,wing_ext,bank_deg\n");

	const FPhase Glide[] = { { 30.0, Make(Dir(0.0, 0.0), true, false, false) } };
	Run("glide", Cruise(11.0, 100.0), Glide, 1);

	const FPhase Stall[] = { { 15.0, Make(Dir(25.0, 0.0), true, false, false) } };
	Run("nose_high_stall", Cruise(12.0, 100.0), Stall, 1);

	const FPhase DivePull[] = {
		{ 4.0, Make(Dir(-60.0, 0.0), false, false, true) },
		{ 6.0, Make(Dir(30.0, 0.0), true, false, false) },
		{ 6.0, Make(Dir(0.0, 0.0), true, false, false) } };
	Run("dive_and_pullup", Cruise(12.0, 150.0), DivePull, 3);

	const FPhase Flap[] = { { 15.0, Make(Dir(5.0, 0.0), false, true, false) } };
	Run("flap_climb", Cruise(12.0, 20.0), Flap, 1);

	FFlightState Rest;
	Rest.WingExtension = 0.5;
	const FPhase Takeoff[] = {
		{ 3.0, Make(Dir(30.0, 0.0), false, true, false) },
		{ 5.0, Make(Dir(0.0, 0.0), true, false, false) } };
	Run("takeoff_then_glide", Rest, Takeoff, 2);

	FWindParams Headwind;
	Headwind.BaseWind = FVec3(-5.0, 0.0, 0.0);
	FFlightState HeadwindStart = Cruise(11.0, 100.0);
	HeadwindStart.Velocity += Headwind.BaseWind;
	Run("glide_5ms_headwind", HeadwindStart, Glide, 1, &Headwind);

	FWindParams Gusty;
	Gusty.GustStrengthMps = 3.0;
	Run("glide_gusty", Cruise(11.0, 100.0), Glide, 1, &Gusty);

	return 0;
}
