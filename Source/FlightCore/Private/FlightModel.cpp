#include "FlightCore/FlightModel.h"

#include <cmath>

namespace BirdFlight
{
	namespace
	{
		constexpr double DegToRad = Pi / 180.0;
		constexpr double MaxPitchRad = 88.0 * DegToRad;
		const FVec3 WorldUp(0.0, 0.0, 1.0);

		// Wrap to [-Pi, Pi].
		double WrapAngle(double A)
		{
			A = std::fmod(A + Pi, 2.0 * Pi);
			if (A < 0.0)
			{
				A += 2.0 * Pi;
			}
			return A - Pi;
		}

		double FiniteOr(double V, double Fallback) { return std::isfinite(V) ? V : Fallback; }

		// Repair state the wrapper or network may hand us (NaNs, non-unit vectors). Up is rebuilt
		// from Forward every substep, so it needs no repair here. Position is the caller's.
		void SanitizeState(FFlightState& State, const FFlightParams& Params)
		{
			if (!State.Velocity.IsFinite())
			{
				State.Velocity = FVec3();
			}
			State.Forward = State.Forward.GetSafeNormal(FVec3(1.0, 0.0, 0.0));
			State.BankRad = FiniteOr(State.BankRad, 0.0);
			State.WingExtension = Clamp(FiniteOr(State.WingExtension, Params.RelaxedExtension), 0.0, 1.0);
			State.FlapPhase = FiniteOr(State.FlapPhase, 0.0);
			State.FlapPhase -= std::floor(State.FlapPhase);
		}

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

			// Steer heading (yaw about world up) and pitch separately rather than along the great
			// circle: a great-circle turn toward an aim behind the bird would pitch the nose through
			// vertical (a split-S) and flip the roll. Pitch is clamped short of vertical so heading
			// and the wings-level frame are always defined.
			const double OldYaw = std::atan2(State.Forward.Y, State.Forward.X);
			const double OldPitch = std::asin(Clamp(State.Forward.Z, -1.0, 1.0));
			const double AimHorizontal = std::sqrt(Aim.X * Aim.X + Aim.Y * Aim.Y);
			const double AimYaw = AimHorizontal > 1e-3 ? std::atan2(Aim.Y, Aim.X) : OldYaw;
			const double AimPitch = Clamp(std::asin(Clamp(Aim.Z, -1.0, 1.0)), -MaxPitchRad, MaxPitchRad);

			const double YawDelta = WrapAngle(AimYaw - OldYaw);
			const double PitchDelta = AimPitch - OldPitch;
			// The nose sweeps yaw * cos(pitch) of arc; floor the weight so steep dives can't spin.
			const double YawWeight = std::fmax(std::cos(OldPitch), 0.5);
			const double ArcToAim = std::sqrt(YawDelta * YawDelta * YawWeight * YawWeight + PitchDelta * PitchDelta);
			const double MaxArc = TurnRate * Dt;
			const double StepFraction = ArcToAim > MaxArc ? MaxArc / ArcToAim : 1.0;

			const double NewYaw = OldYaw + YawDelta * StepFraction;
			const double NewPitch = Clamp(OldPitch + PitchDelta * StepFraction, -MaxPitchRad, MaxPitchRad);
			const double YawRate = YawDelta * StepFraction / Dt;

			// Coordinated-turn bank: tan(bank) = v * yawRate / g.
			const double MaxBank = Params.MaxBankDeg * DegToRad;
			const double DesiredBank = Clamp(std::atan(Airspeed * YawRate / Params.Gravity), -MaxBank, MaxBank);
			State.BankRad = MoveToward(State.BankRad, DesiredBank, Params.BankRateDegPerSec * DegToRad * Dt);

			// Exact orthonormal frame from yaw/pitch, then roll Up toward Right by the bank angle.
			const double CY = std::cos(NewYaw), SY = std::sin(NewYaw);
			const double CP = std::cos(NewPitch), SP = std::sin(NewPitch);
			State.Forward = FVec3(CP * CY, CP * SY, SP);
			const FVec3 LevelUp(-SP * CY, -SP * SY, CP);
			const FVec3 LevelRight(-SY, CY, 0.0);
			State.Up = LevelUp * std::cos(State.BankRad) + LevelRight * std::sin(State.BankRad);
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
					(std::fabs(Alpha) - GetStallAngleRad(Params)) / std::fmax(Params.StallFalloffRad, 1e-6), 0.0, 1.0);
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
		const double StallProgress = Clamp((AbsAlpha - StallAngle) / std::fmax(Params.StallFalloffRad, 1e-6), 0.0, 1.0);
		// Fades to zero as the airflow comes from straight below/behind (alpha -> 90 deg and beyond).
		const double HighAlphaFade = Clamp(std::cos(AbsAlpha) / std::cos(StallAngle), 0.0, 1.0);
		return Sign * Params.MaxLiftCoeff * Lerp(1.0, Params.PostStallLiftFraction, StallProgress) * HighAlphaFade;
	}

	FFlightTelemetry UpdateVelocityAndOrientation(FFlightState& State, const FFlightInput& Input,
		const FFlightParams& Params, const FVec3& Wind, double Dt)
	{
		SanitizeState(State, Params);
		const FVec3 SafeWind = Wind.IsFinite() ? Wind : FVec3();
		if (!(Dt > 0.0) || !std::isfinite(Dt))
		{
			return ComputeForces(State, Input, Params, SafeWind);
		}
		UpdateWings(State, Input, Params, Dt);
		UpdateOrientation(State, Input, Params, SafeWind, Dt);
		const FFlightTelemetry T = ComputeForces(State, Input, Params, SafeWind);

		// Semi-implicit Euler (velocity first; the caller moves with the new velocity).
		const FVec3 Accel = T.TotalForce / Params.MassKg + FVec3(0.0, 0.0, -Params.Gravity);
		State.Velocity += Accel * Dt;
		return T;
	}

	FFlightTelemetry Step(FFlightState& State, const FFlightInput& Input, const FFlightParams& Params,
		const FVec3& Wind, double DeltaSec)
	{
		if (!(DeltaSec > 0.0) || !std::isfinite(DeltaSec))
		{
			return UpdateVelocityAndOrientation(State, Input, Params, Wind, 0.0);
		}
		const double Dt = std::fmin(DeltaSec, Params.MaxStepSec);
		const double Substep = std::fmax(Params.MaxSubstepSec, 1e-4);
		const int Count = static_cast<int>(Clamp(std::ceil(Dt / Substep - 1e-9), 1.0, 1000.0));
		const double H = Dt / Count;

		FFlightTelemetry T;
		for (int i = 0; i < Count; ++i)
		{
			T = UpdateVelocityAndOrientation(State, Input, Params, Wind, H);
			State.Position += State.Velocity * H;
		}
		return T;
	}

	void SanitizeParams(FFlightParams& P)
	{
		P.MassKg = std::fmax(FiniteOr(P.MassKg, 0.45), 0.01);
		P.Gravity = std::fmax(FiniteOr(P.Gravity, 9.81), 0.0);
		P.AirDensity = std::fmax(FiniteOr(P.AirDensity, 1.225), 0.0);
		P.WingAreaM2 = std::fmax(FiniteOr(P.WingAreaM2, 0.0), 0.0);
		P.AspectRatio = std::fmax(FiniteOr(P.AspectRatio, 6.0), 0.5);
		P.OswaldEfficiency = Clamp(FiniteOr(P.OswaldEfficiency, 0.8), 0.1, 1.0);
		P.LiftSlopePerRad = std::fmax(FiniteOr(P.LiftSlopePerRad, 4.5), 0.1);
		P.MaxLiftCoeff = std::fmax(FiniteOr(P.MaxLiftCoeff, 1.4), 0.01);
		P.PostStallLiftFraction = Clamp(FiniteOr(P.PostStallLiftFraction, 0.5), 0.0, 1.0);
		P.StallFalloffRad = std::fmax(FiniteOr(P.StallFalloffRad, 0.3), 0.01);
		P.WingParasiticDragCoeff = std::fmax(FiniteOr(P.WingParasiticDragCoeff, 0.0), 0.0);
		P.FlatPlateDragCoeff = std::fmax(FiniteOr(P.FlatPlateDragCoeff, 0.0), 0.0);
		P.BodyDragAreaM2 = std::fmax(FiniteOr(P.BodyDragAreaM2, 0.0), 0.0);
		P.SideForceAreaM2PerRad = std::fmax(FiniteOr(P.SideForceAreaM2PerRad, 0.0), 0.0);
		P.RelaxedExtension = Clamp(FiniteOr(P.RelaxedExtension, 0.5), 0.0, 1.0);
		P.ExtensionRatePerSec = std::fmax(FiniteOr(P.ExtensionRatePerSec, 5.0), 0.0);
		P.FlapThrustN = std::fmax(FiniteOr(P.FlapThrustN, 0.0), 0.0);
		P.FlapThrustFadeSpeed = std::fmax(FiniteOr(P.FlapThrustFadeSpeed, 24.0), 0.1);
		P.FlapLiftN = std::fmax(FiniteOr(P.FlapLiftN, 0.0), 0.0);
		P.FlapLiftFadeSpeed = std::fmax(FiniteOr(P.FlapLiftFadeSpeed, 8.0), 0.1);
		P.FlapFrequencyHz = std::fmax(FiniteOr(P.FlapFrequencyHz, 4.0), 0.0);
		P.FlapPulseAmount = Clamp(FiniteOr(P.FlapPulseAmount, 0.5), 0.0, 1.0);
		P.MaxTurnRateDegPerSec = std::fmax(FiniteOr(P.MaxTurnRateDegPerSec, 150.0), 0.0);
		P.TuckedTurnRateScale = Clamp(FiniteOr(P.TuckedTurnRateScale, 0.35), 0.0, 1.0);
		P.MaxBankDeg = Clamp(FiniteOr(P.MaxBankDeg, 65.0), 0.0, 85.0);
		P.BankRateDegPerSec = std::fmax(FiniteOr(P.BankRateDegPerSec, 240.0), 0.0);
		P.StallNoseDrop = Clamp(FiniteOr(P.StallNoseDrop, 0.6), 0.0, 1.0);
		P.MaxSubstepSec = Clamp(FiniteOr(P.MaxSubstepSec, 1.0 / 120.0), 1e-4, 0.1);
		P.MaxStepSec = std::fmax(FiniteOr(P.MaxStepSec, 0.25), P.MaxSubstepSec);
	}
}
