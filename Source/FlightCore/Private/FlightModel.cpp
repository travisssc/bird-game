#include "FlightCore/FlightModel.h"

#include <algorithm>
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

		// Largest angle (radians) the nose may sit off the oncoming air before the wings and tail
		// would exceed the load-factor cap, using the linear (pre-stall) force slopes. Above the
		// corner speed this is smaller than the stall angle; below it the cap never binds.
		double MaxNoseOffAirflowRad(const FFlightParams& Params, double Extension, double Airspeed)
		{
			const double Q = 0.5 * Params.AirDensity * Airspeed * Airspeed;
			const double ForcePerRad = Q * (Params.WingAreaM2 * Extension * Params.LiftSlopePerRad
				+ Params.SideForceAreaM2PerRad);
			const double MaxForce = Params.MaxLoadFactor * Params.MassKg * Params.Gravity;
			return ForcePerRad > TinyNumber ? MaxForce / ForcePerRad : Pi;
		}

		void UpdateOrientation(FFlightState& State, const FFlightInput& Input, const FFlightParams& Params,
			const FVec3& Wind, double Dt)
		{
			const FVec3 AirVelocity = State.Velocity - Wind;
			const double Airspeed = AirVelocity.Size();

			FVec3 Aim = Input.AimDirection.GetSafeNormal(State.Forward);

			// A stalled bird can't hold its nose up: it drops toward the oncoming air, which is
			// also how a stall recovers. Flapping overrides this (birds can flap out of a stall).
			const bool bStalled = IsStalled(Params, AngleOfAttack(State, AirVelocity), Airspeed);
			if (!Input.bFlap && bStalled)
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
			double AimPitch = std::asin(Clamp(Aim.Z, -1.0, 1.0));

			// Glide trim: a gliding bird holds its best-glide attitude when the player looks at the
			// horizon, fading out as the aim moves away from level (so dives and climbs still work).
			// Monotonic in the aim as long as the trim is smaller than the fade angle.
			if (!Input.bFlap && !Input.bTuck && !bStalled && Params.GlideTrimAssist > 0.0)
			{
				const double Fade = std::fmax(Params.GlideTrimFadeDeg * DegToRad, 1e-3);
				const double Weight = Clamp(1.0 - std::fabs(AimPitch) / Fade, 0.0, 1.0);
				const double Trim = Clamp(GetBestGlidePitchRad(Params, State.WingExtension), -0.5 * Fade, 0.5 * Fade);
				AimPitch += Params.GlideTrimAssist * Trim * Weight;
			}
			AimPitch = Clamp(AimPitch, -MaxPitchRad, MaxPitchRad);

			const double YawDelta = WrapAngle(AimYaw - OldYaw);
			const double PitchDelta = AimPitch - OldPitch;
			// The nose sweeps yaw * cos(pitch) of arc; floor the weight so steep dives can't spin.
			const double YawWeight = std::fmax(std::cos(OldPitch), 0.5);
			const double ArcToAim = std::sqrt(YawDelta * YawDelta * YawWeight * YawWeight + PitchDelta * PitchDelta);
			const double MaxArc = TurnRate * Dt;
			const double StepFraction = ArcToAim > MaxArc ? MaxArc / ArcToAim : 1.0;

			double NewYaw = OldYaw + YawDelta * StepFraction;
			double NewPitch = Clamp(OldPitch + PitchDelta * StepFraction, -MaxPitchRad, MaxPitchRad);

			// Load-factor cap: at high speed even a small angle to the airflow makes a huge force, so
			// the nose is held within the angle that produces MaxLoadFactor. The flight path then
			// bends at that g and the nose rides along it (a carved pull-out, not a snap).
			if (Airspeed > 1.0)
			{
				const double MaxOff = MaxNoseOffAirflowRad(Params, State.WingExtension, Airspeed);
				if (MaxOff < GetStallAngleRad(Params))
				{
					const double CY = std::cos(NewYaw), SY = std::sin(NewYaw);
					const double CP = std::cos(NewPitch), SP = std::sin(NewPitch);
					const FVec3 Candidate(CP * CY, CP * SY, SP);
					const FVec3 AirDirection = AirVelocity / Airspeed;
					if (std::acos(Clamp(Dot(Candidate, AirDirection), -1.0, 1.0)) > MaxOff)
					{
						const FVec3 Limited = RotateToward(AirDirection, Candidate, MaxOff, State.GetRight());
						const double LimitedHorizontal = std::sqrt(Limited.X * Limited.X + Limited.Y * Limited.Y);
						if (LimitedHorizontal > 1e-3)
						{
							NewYaw = OldYaw + WrapAngle(std::atan2(Limited.Y, Limited.X) - OldYaw);
						}
						NewPitch = Clamp(std::asin(Clamp(Limited.Z, -1.0, 1.0)), -MaxPitchRad, MaxPitchRad);
					}
				}
			}
			// Bank like a coordinated turn: tan(bank) = v * turnRate / g, from the nose's yaw rate.
			// After the nose stops, the flight path is still catching up with it, so the bank is held
			// (and eases off) at the rate the path must turn to close the gap: the end of a turn
			// carves like a bike leaning out of a corner instead of rolling level and skidding. That
			// catch-up term only ever holds a bank the turn already built; it never starts one, so
			// wind moving the path (gusts, a breeze from behind on takeoff) can't roll the bird.
			const double AirHorizontal = std::sqrt(AirVelocity.X * AirVelocity.X + AirVelocity.Y * AirVelocity.Y);
			const bool bTrackBank = Params.BankTrackBlend > 0.0 && AirHorizontal > 1.0;
			const double TrackYaw = bTrackBank ? std::atan2(AirVelocity.Y, AirVelocity.X) : 0.0;
			const double MaxBank = Params.MaxBankDeg * DegToRad;
			const double MaxBankStep = Params.BankRateDegPerSec * DegToRad * Dt;
			const double LastBank = State.BankRad;
			auto BankForYaw = [&](double Yaw)
			{
				const double YawRateNow = WrapAngle(Yaw - OldYaw) / Dt;
				double Desired = Clamp(std::atan(Airspeed * YawRateNow / Params.Gravity), -MaxBank, MaxBank);
				if (bTrackBank)
				{
					const double CatchUpRate = WrapAngle(Yaw - TrackYaw) / Params.BankTrackTimeSec;
					const double CatchUp = Params.BankTrackBlend
						* Clamp(std::atan(Airspeed * CatchUpRate / Params.Gravity), -MaxBank, MaxBank);
					const double Hold = CatchUp * LastBank > 0.0
						? (CatchUp > 0.0 ? 1.0 : -1.0) * (std::min)(std::fabs(CatchUp), std::fabs(LastBank))
						: 0.0;
					if (Hold * Desired >= 0.0 && std::fabs(Hold) > std::fabs(Desired))
					{
						Desired = Hold;
					}
				}
				return MoveToward(LastBank, Desired, MaxBankStep);
			};

			// Turn stall protection: yawing the nose ahead of the flight path in a bank raises the
			// wing's angle of attack, and a 150 deg/s camera flick would stall the bird mid-turn. A
			// real bird turns wider instead, so the yaw step is shortened to keep the wing below the
			// stall. Pitching up can still stall the bird; only the turn is protected.
			if (Airspeed > 1.0 && WrapAngle(NewYaw - OldYaw) != 0.0)
			{
				auto AlphaForYaw = [&](double Yaw)
				{
					const double Bank = BankForYaw(Yaw);
					const double CY = std::cos(Yaw), SY = std::sin(Yaw);
					const double CP = std::cos(NewPitch), SP = std::sin(NewPitch);
					const FVec3 Fwd(CP * CY, CP * SY, SP);
					const FVec3 Up = FVec3(-SP * CY, -SP * SY, CP) * std::cos(Bank) + FVec3(-SY, CY, 0.0) * std::sin(Bank);
					return std::fabs(std::atan2(-Dot(AirVelocity, Up), Dot(AirVelocity, Fwd)));
				};
				const double Protect = (std::max)(Params.TurnStallMargin * GetStallAngleRad(Params), AlphaForYaw(OldYaw));
				const double FullStep = WrapAngle(NewYaw - OldYaw);
				if (AlphaForYaw(NewYaw) > Protect)
				{
					double Lo = 0.0, Hi = 1.0;
					for (int i = 0; i < 10; ++i)
					{
						const double Mid = 0.5 * (Lo + Hi);
						(AlphaForYaw(OldYaw + FullStep * Mid) > Protect ? Hi : Lo) = Mid;
					}
					NewYaw = OldYaw + FullStep * Lo;
				}
			}
			State.BankRad = BankForYaw(NewYaw);

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
				double CL = ComputeLiftCoeff(Params, Alpha);
				const double Q = 0.5 * Params.AirDensity * Airspeed * Airspeed;
				const double WingArea = Params.WingAreaM2 * Extension;

				// Lift: perpendicular to the airflow, on the bird's "up" side.
				const FVec3 LiftDir = (State.Up - AirDirection * Dot(State.Up, AirDirection)).GetSafeNormal();
				T.LiftForce = LiftDir * (Q * WingArea * CL);

				// Side force from tail/body: opposes sideslip so velocity follows the heading.
				const double Sideslip = std::atan2(Dot(AirVelocity, Right), std::fabs(Dot(AirVelocity, State.Forward)));
				FVec3 SideForce = Right * (-Q * Params.SideForceAreaM2PerRad * Sideslip);

				// Load-factor cap (backstop for the orientation limit): the bird eases its wings
				// rather than pull more than MaxLoadFactor. Induced drag follows the eased lift.
				const double MaxPerpForce = Params.MaxLoadFactor * Params.MassKg * Params.Gravity;
				const double PerpForce = (T.LiftForce + SideForce).Size();
				if (PerpForce > MaxPerpForce)
				{
					const double Ease = MaxPerpForce / PerpForce;
					T.LiftForce *= Ease;
					SideForce *= Ease;
					CL *= Ease;
				}

				T.AngleOfAttackRad = Alpha;
				T.LiftCoeff = CL;
				T.bStalled = IsStalled(Params, Alpha, Airspeed);

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

			T.LoadFactor = T.TotalForce.Size() / (Params.MassKg * std::fmax(Params.Gravity, TinyNumber));
			return T;
		}
	}

	double GetStallAngleRad(const FFlightParams& Params)
	{
		return Params.MaxLiftCoeff / Params.LiftSlopePerRad;
	}

	double GetBestGlidePitchRad(const FFlightParams& Params, double WingExtension)
	{
		// Best glide is where profile drag equals induced drag: CL = sqrt(CD0 / k).
		const double WingArea = Params.WingAreaM2 * Clamp(WingExtension, 0.0, 1.0);
		if (WingArea < 1e-6)
		{
			return 0.0;
		}
		const double K = 1.0 / (Pi * Params.AspectRatio * Params.OswaldEfficiency);
		const double CD0 = Params.WingParasiticDragCoeff + Params.BodyDragAreaM2 / WingArea;
		const double CL = (std::min)(std::sqrt(CD0 / K), 0.85 * Params.MaxLiftCoeff);
		const double Alpha = CL / Params.LiftSlopePerRad;
		const double GlideAngle = std::atan((CD0 + K * CL * CL) / CL);
		return Alpha - GlideAngle;
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
		P.Gravity = std::fmax(FiniteOr(P.Gravity, 9.81), 0.1);
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
		P.MaxLoadFactor = std::fmax(FiniteOr(P.MaxLoadFactor, 7.0), 1.5);
		P.GlideTrimAssist = Clamp(FiniteOr(P.GlideTrimAssist, 1.0), 0.0, 1.0);
		P.GlideTrimFadeDeg = Clamp(FiniteOr(P.GlideTrimFadeDeg, 30.0), 1.0, 89.0);
		P.BankTrackBlend = Clamp(FiniteOr(P.BankTrackBlend, 1.0), 0.0, 1.0);
		P.BankTrackTimeSec = std::fmax(FiniteOr(P.BankTrackTimeSec, 0.25), 0.05);
		P.TurnStallMargin = Clamp(FiniteOr(P.TurnStallMargin, 0.9), 0.1, 2.0);
		P.MaxSubstepSec = Clamp(FiniteOr(P.MaxSubstepSec, 1.0 / 120.0), 1e-4, 0.1);
		P.MaxStepSec = std::fmax(FiniteOr(P.MaxStepSec, 0.25), P.MaxSubstepSec);
	}
}
