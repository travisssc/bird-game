#include "FlightCore/LookControl.h"

#include <cmath>

namespace BirdFlight
{
	namespace
	{
		constexpr double DegToRad = Pi / 180.0;
		constexpr double ReturnDoneRad = 0.5 * DegToRad;

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

		FVec3 DirFromYawPitch(double Yaw, double Pitch)
		{
			return FVec3(std::cos(Pitch) * std::cos(Yaw), std::cos(Pitch) * std::sin(Yaw), std::sin(Pitch));
		}

		void Sanitize(FLookState& S)
		{
			S.AimYawRad = WrapAngle(FiniteOr(S.AimYawRad, 0.0));
			S.AimPitchRad = FiniteOr(S.AimPitchRad, 0.0);
			S.CameraYawRad = WrapAngle(FiniteOr(S.CameraYawRad, S.AimYawRad));
			S.CameraPitchRad = FiniteOr(S.CameraPitchRad, S.AimPitchRad);
			S.HeadYawRad = FiniteOr(S.HeadYawRad, 0.0);
			S.HeadPitchRad = FiniteOr(S.HeadPitchRad, 0.0);
		}

		// Clamp a head yaw target to the neck. Past the limit (looking behind), the head stays on
		// the side it's already on, so it doesn't flick across when the camera passes behind.
		double ClampHeadYaw(double Target, double Current, double Limit)
		{
			if (std::fabs(Target) <= Limit)
			{
				return Target;
			}
			const double Side = Current != 0.0 ? (Current > 0.0 ? 1.0 : -1.0) : (Target > 0.0 ? 1.0 : -1.0);
			return Side * Limit;
		}
	}

	FVec3 FLookState::GetAimDirection() const { return DirFromYawPitch(AimYawRad, AimPitchRad); }
	FVec3 FLookState::GetCameraDirection() const { return DirFromYawPitch(CameraYawRad, CameraPitchRad); }

	void DirectionToBodyAngles(const FFlightState& Body, const FVec3& Direction, double& OutYawRad, double& OutPitchRad)
	{
		const FVec3 D = Direction.GetSafeNormal(Body.Forward);
		const double LocalX = Dot(D, Body.Forward);
		const double LocalY = Dot(D, Body.GetRight());
		const double LocalZ = Dot(D, Body.Up);
		OutYawRad = std::atan2(LocalY, LocalX);
		OutPitchRad = std::asin(Clamp(LocalZ, -1.0, 1.0));
	}

	void UpdateLook(FLookState& State, const FLookInput& Input, const FFlightState& Body,
		const FLookParams& Params, double DeltaSec)
	{
		Sanitize(State);
		const double Dt = (DeltaSec > 0.0 && std::isfinite(DeltaSec)) ? DeltaSec : 0.0;
		const double PitchMin = Params.PitchMinDeg * DegToRad;
		const double PitchMax = Params.PitchMaxDeg * DegToRad;
		const double DYaw = FiniteOr(Input.MouseYawDeg, 0.0) * DegToRad;
		const double DPitch = FiniteOr(Input.MousePitchDeg, 0.0) * DegToRad;

		// Mode changes. Re-pressing Alt mid-return picks up the camera where it is.
		if (Input.bFreeLook)
		{
			State.Mode = ELookMode::FreeLook;
		}
		else if (State.Mode == ELookMode::FreeLook)
		{
			State.Mode = ELookMode::Returning;
		}

		if (State.Mode == ELookMode::FreeLook)
		{
			State.CameraYawRad = WrapAngle(State.CameraYawRad + DYaw);
			State.CameraPitchRad = Clamp(State.CameraPitchRad + DPitch, PitchMin, PitchMax);
		}
		else
		{
			// The mouse always steers outside free look, including while the camera swings back.
			State.AimYawRad = WrapAngle(State.AimYawRad + DYaw);
			State.AimPitchRad = Clamp(State.AimPitchRad + DPitch, PitchMin, PitchMax);
			if (State.Mode == ELookMode::Returning)
			{
				// Exponential ease: ~99% of the way back after ReturnTimeSec.
				const double Ease = 1.0 - std::exp(-Dt * 4.6 / std::fmax(Params.ReturnTimeSec, 1e-3));
				const double YawGap = WrapAngle(State.AimYawRad - State.CameraYawRad);
				const double PitchGap = State.AimPitchRad - State.CameraPitchRad;
				State.CameraYawRad = WrapAngle(State.CameraYawRad + YawGap * Ease);
				State.CameraPitchRad += PitchGap * Ease;
				if (std::sqrt(YawGap * YawGap + PitchGap * PitchGap) * (1.0 - Ease) < ReturnDoneRad)
				{
					State.Mode = ELookMode::Flying;
				}
			}
			if (State.Mode == ELookMode::Flying)
			{
				State.CameraYawRad = State.AimYawRad;
				State.CameraPitchRad = State.AimPitchRad;
			}
		}

		// Head: looks where the camera looks in free look, and leads into the aim otherwise.
		double TargetYaw = 0.0, TargetPitch = 0.0;
		const bool bLookingAround = State.Mode != ELookMode::Flying;
		DirectionToBodyAngles(Body, bLookingAround ? State.GetCameraDirection() : State.GetAimDirection(),
			TargetYaw, TargetPitch);
		if (!bLookingAround)
		{
			TargetYaw *= Params.HeadLeadFraction;
			TargetPitch *= Params.HeadLeadFraction;
		}
		TargetYaw = ClampHeadYaw(TargetYaw, State.HeadYawRad, Params.NeckYawLimitDeg * DegToRad);
		TargetPitch = Clamp(TargetPitch, -Params.NeckPitchDownDeg * DegToRad, Params.NeckPitchUpDeg * DegToRad);
		const double MaxHeadStep = Params.HeadTurnRateDegPerSec * DegToRad * Dt;
		State.HeadYawRad = MoveToward(State.HeadYawRad, TargetYaw, MaxHeadStep);
		State.HeadPitchRad = MoveToward(State.HeadPitchRad, TargetPitch, MaxHeadStep);
	}
}
