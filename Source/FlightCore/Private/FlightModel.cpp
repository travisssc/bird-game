#include "FlightCore/FlightModel.h"

#include <cmath>

namespace BirdFlight
{
	namespace
	{
		constexpr double DegToRad = Pi / 180.0;
		const FVec3 WorldUp(0.0, 0.0, 1.0);

		double TargetWingExtension(const FFlightInput& Input, const FFlightParams& Params)
		{
			// Priority: flap > tuck > glide > relaxed. Flapping out of a dive should work instantly.
			if (Input.bFlap)  { return 1.0; }
			if (Input.bTuck)  { return 0.0; }
			if (Input.bGlide) { return 1.0; }
			return Params.RelaxedExtension;
		}

		double AngleOfAttack(const FFlightState& State, const FVec3& AirVelocity)
		{
			// Positive when the airflow hits the underside of the wings.
			return std::atan2(-Dot(AirVelocity, State.Up), Dot(AirVelocity, State.Forward));
		}

		bool IsStalled(const FFlightParams& Params, double AngleOfAttackRad, double Airspeed)
		{
			return Airspeed > 0.5 && std::fabs(AngleOfAttackRad) > GetStallAngleRad(Params);
		}

		void UpdateWings(FFlightState& State, const FFlightInput& Input, const FFlightParams& Params, double Dt)
		{
			State.WingExtension = MoveToward(State.WingExtension, TargetWingExtension(Input, Params),
				Params.ExtensionRatePerSec * Dt);
			if (Input.bFlap)
			{
				State.FlapPhase = std::fmod(State.FlapPhase + Params.FlapFrequencyHz * Dt, 1.0);
			}
		}

		void UpdateOrientation(FFlightState& State, const FFlightInput& Input, const FFlightParams& Params,
			const FVec3& Wind, double Dt)
		{
			const FVec3 AirVelocity = State.Velocity - Wind;
			const double Airspeed = AirVelocity.Size();

			FVec3 Aim = Input.AimDirection.GetSafeNormal(State.Forward);

			// A stalled bird can't hold its nose up: it drops toward the oncoming air, which is
			// also how a stall recovers. Flapping overrides this (birds can flap out of a stall).
			if (!Input.bFlap && IsStalled(Params, AngleOfAttack(State, AirVelocity), Airspeed))
			{
				const FVec3 AirDirection = AirVelocity / Airspeed;
				Aim = (Aim * (1.0 - Params.StallNoseDrop) + AirDirection * Params.StallNoseDrop).GetSafeNormal(Aim);
			}

			const double TurnRate = Params.MaxTurnRateDegPerSec * DegToRad
				* Lerp(Params.TuckedTurnRateScale, 1.0, State.WingExtension);
			const FVec3 OldForward = State.Forward;
			const FVec3 NewForward = RotateToward(OldForward, Aim, TurnRate * Dt, State.Up);

			// Yaw rate (about world up) drives a coordinated-turn bank: tan(bank) = v * yawRate / g.
			double YawRate = 0.0;
			const FVec3 OldFlat(OldForward.X, OldForward.Y, 0.0);
			const FVec3 NewFlat(NewForward.X, NewForward.Y, 0.0);
			if (OldFlat.Size() > 0.05 && NewFlat.Size() > 0.05)
			{
				const FVec3 A = OldFlat.GetSafeNormal();
				const FVec3 B = NewFlat.GetSafeNormal();
				YawRate = std::atan2(Cross(A, B).Z, Dot(A, B)) / Dt;
			}
			const double MaxBank = Params.MaxBankDeg * DegToRad;
			const double DesiredBank = Clamp(std::atan(Airspeed * YawRate / Params.Gravity), -MaxBank, MaxBank);
			State.BankRad = MoveToward(State.BankRad, DesiredBank, Params.BankRateDegPerSec * DegToRad * Dt);

			State.Forward = NewForward;

			// Up = the wings-level up vector for this heading, rolled by the bank angle.
			const FVec3 LevelUp = WorldUp - NewForward * Dot(WorldUp, NewForward);
			if (LevelUp.Size() > 0.05)
			{
				const FVec3 LevelUpN = LevelUp.GetSafeNormal();
				const FVec3 LevelRight = Cross(LevelUpN, NewForward);
				State.Up = LevelUpN * std::cos(State.BankRad) + LevelRight * std::sin(State.BankRad);
			}
			else
			{
				// Pointing (nearly) straight up or down: "level" is undefined, keep the previous roll.
				State.Up = State.Up - NewForward * Dot(State.Up, NewForward);
			}
			State.Up = (State.Up - NewForward * Dot(State.Up, NewForward)).GetSafeNormal(FVec3(0.0, 0.0, 1.0));
		}

		FFlightTelemetry ComputeForces(const FFlightState& State, const FFlightInput& Input,
			const FFlightParams& Params, const FVec3& Wind)
		{
			FFlightTelemetry T;
			const FVec3 AirVelocity = State.Velocity - Wind;
			const double Airspeed = AirVelocity.Size();
			const double Extension = State.WingExtension;
			T.AirspeedMps = Airspeed;

			if (Airspeed > 1e-3)
			{
				const FVec3 AirDirection = AirVelocity / Airspeed;
				const FVec3 Right = State.GetRight();
				const double Alpha = AngleOfAttack(State, AirVelocity);
				const double CL = ComputeLiftCoeff(Params, Alpha);
				const double Q = 0.5 * Params.AirDensity * Airspeed * Airspeed;
				const double WingArea = Params.WingAreaM2 * Extension;

				T.AngleOfAttackRad = Alpha;
				T.LiftCoeff = CL;
				T.bStalled = IsStalled(Params, Alpha, Airspeed);

				// Lift: perpendicular to the airflow, on the bird's "up" side.
				const FVec3 LiftDir = (State.Up - AirDirection * Dot(State.Up, AirDirection)).GetSafeNormal();
				T.LiftForce = LiftDir * (Q * WingArea * CL);

				// Drag: profile + induced + flat-plate on the wings, plus the body. Flat-plate drag
				// models separated flow, so it blends in only past the stall angle.
				const double InducedCD = CL * CL / (Pi * Params.AspectRatio * Params.OswaldEfficiency);
				const double SinAlpha = std::sin(Alpha);
				const double StallProgress = Clamp(
					(std::fabs(Alpha) - GetStallAngleRad(Params)) / Params.StallFalloffRad, 0.0, 1.0);
				const double FlatPlateCD = Params.FlatPlateDragCoeff * SinAlpha * SinAlpha * StallProgress;
				const double DragMag = Q * (WingArea * (Params.WingParasiticDragCoeff + InducedCD + FlatPlateCD)
					+ Params.BodyDragAreaM2);
				T.DragForce = AirDirection * -DragMag;

				// Side force from tail/body: opposes sideslip so velocity follows the heading.
				const double Sideslip = std::atan2(Dot(AirVelocity, Right), std::fabs(Dot(AirVelocity, State.Forward)));
				const FVec3 SideForce = Right * (-Q * Params.SideForceAreaM2PerRad * Sideslip);

				T.TotalForce = T.LiftForce + T.DragForce + SideForce;
			}

			if (Input.bFlap)
			{
				// Average thrust/lift modulated per wingbeat (downstroke strong, upstroke weak).
				// Low-speed flap lift points straight up: birds tilt their stroke plane at takeoff, so a
				// nose-up bird must not get pushed backwards by its own wingbeats.
				const double Pulse = 1.0 + Params.FlapPulseAmount * std::sin(2.0 * Pi * State.FlapPhase);
				const double ForwardAirspeed = Dot(AirVelocity, State.Forward);
				const double ThrustScale = Clamp(1.0 - ForwardAirspeed / Params.FlapThrustFadeSpeed, 0.0, 1.0);
				const double FlapLiftScale = Clamp(1.0 - Airspeed / Params.FlapLiftFadeSpeed, 0.0, 1.0);
				T.ThrustForce = State.Forward * (Params.FlapThrustN * ThrustScale * Pulse * Extension)
					+ WorldUp * (Params.FlapLiftN * FlapLiftScale * Pulse * Extension);
				T.TotalForce += T.ThrustForce;
			}

			return T;
		}

		FFlightTelemetry Substep(FFlightState& State, const FFlightInput& Input, const FFlightParams& Params,
			const FVec3& Wind, double Dt)
		{
			UpdateWings(State, Input, Params, Dt);
			UpdateOrientation(State, Input, Params, Wind, Dt);
			const FFlightTelemetry T = ComputeForces(State, Input, Params, Wind);

			// Semi-implicit Euler: stable for this kind of damped system at 120 Hz.
			const FVec3 Accel = T.TotalForce / Params.MassKg + FVec3(0.0, 0.0, -Params.Gravity);
			State.Velocity += Accel * Dt;
			State.Position += State.Velocity * Dt;
			return T;
		}
	}

	double GetStallAngleRad(const FFlightParams& Params)
	{
		return Params.MaxLiftCoeff / Params.LiftSlopePerRad;
	}

	double ComputeLiftCoeff(const FFlightParams& Params, double AngleOfAttackRad)
	{
		const double StallAngle = GetStallAngleRad(Params);
		const double AbsAlpha = std::fabs(AngleOfAttackRad);
		if (AbsAlpha <= StallAngle)
		{
			return Params.LiftSlopePerRad * AngleOfAttackRad;
		}
		const double Sign = AngleOfAttackRad < 0.0 ? -1.0 : 1.0;
		const double StallProgress = Clamp((AbsAlpha - StallAngle) / Params.StallFalloffRad, 0.0, 1.0);
		// Fades to zero as the airflow comes from straight below/behind (alpha -> 90 deg and beyond).
		const double HighAlphaFade = Clamp(std::cos(AbsAlpha) / std::cos(StallAngle), 0.0, 1.0);
		return Sign * Params.MaxLiftCoeff * Lerp(1.0, Params.PostStallLiftFraction, StallProgress) * HighAlphaFade;
	}

	FFlightTelemetry Step(FFlightState& State, const FFlightInput& Input, const FFlightParams& Params,
		const FVec3& Wind, double DeltaSec)
	{
		if (!(DeltaSec > 0.0) || !std::isfinite(DeltaSec))
		{
			return ComputeForces(State, Input, Params, Wind);
		}
		const double Dt = DeltaSec < Params.MaxStepSec ? DeltaSec : Params.MaxStepSec;
		const int NumSubsteps = static_cast<int>(std::ceil(Dt / Params.MaxSubstepSec - 1e-9));
		const int Count = NumSubsteps < 1 ? 1 : NumSubsteps;
		const double H = Dt / Count;

		FFlightTelemetry T;
		for (int i = 0; i < Count; ++i)
		{
			T = Substep(State, Input, Params, Wind, H);
		}
		return T;
	}
}
