// Minimal vector math for the engine-agnostic flight core.
//
// Axis convention matches Unreal: X = forward, Y = right, Z = up (left-handed).
// Units are SI (meters, seconds, kilograms). The Unreal wrapper converts cm <-> m.
// Plain C++ only: no Unreal headers, no exceptions, no allocation.
#pragma once

#include <cmath>

namespace BirdFlight
{
	constexpr double Pi = 3.14159265358979323846;
	constexpr double TinyNumber = 1e-9;

	struct FVec3
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;

		constexpr FVec3() = default;
		constexpr FVec3(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}

		constexpr FVec3 operator+(const FVec3& O) const { return FVec3(X + O.X, Y + O.Y, Z + O.Z); }
		constexpr FVec3 operator-(const FVec3& O) const { return FVec3(X - O.X, Y - O.Y, Z - O.Z); }
		constexpr FVec3 operator-() const { return FVec3(-X, -Y, -Z); }
		constexpr FVec3 operator*(double S) const { return FVec3(X * S, Y * S, Z * S); }
		constexpr FVec3 operator/(double S) const { return FVec3(X / S, Y / S, Z / S); }
		FVec3& operator+=(const FVec3& O) { X += O.X; Y += O.Y; Z += O.Z; return *this; }
		FVec3& operator-=(const FVec3& O) { X -= O.X; Y -= O.Y; Z -= O.Z; return *this; }
		FVec3& operator*=(double S) { X *= S; Y *= S; Z *= S; return *this; }

		double SizeSquared() const { return X * X + Y * Y + Z * Z; }
		double Size() const { return std::sqrt(SizeSquared()); }

		// Unit vector, or Fallback if this vector is (near) zero or not finite.
		FVec3 GetSafeNormal() const { return GetSafeNormal(FVec3(0.0, 0.0, 0.0)); }
		FVec3 GetSafeNormal(const FVec3& Fallback) const
		{
			const double Len = Size();
			if (!(Len > TinyNumber) || !std::isfinite(Len))
			{
				return Fallback;
			}
			return *this / Len;
		}

		bool IsFinite() const { return std::isfinite(X) && std::isfinite(Y) && std::isfinite(Z); }
	};

	inline constexpr FVec3 operator*(double S, const FVec3& V) { return V * S; }

	inline double Dot(const FVec3& A, const FVec3& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }

	// Standard cross product. With Unreal axes: Cross(Forward, Right) = Up, Cross(Up, Forward) = Right.
	inline FVec3 Cross(const FVec3& A, const FVec3& B)
	{
		return FVec3(A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X);
	}

	inline double Clamp(double V, double Lo, double Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
	inline double Lerp(double A, double B, double T) { return A + (B - A) * T; }

	// Move Current toward Target by at most MaxDelta.
	inline double MoveToward(double Current, double Target, double MaxDelta)
	{
		const double Delta = Target - Current;
		if (std::fabs(Delta) <= MaxDelta)
		{
			return Target;
		}
		return Current + (Delta > 0.0 ? MaxDelta : -MaxDelta);
	}

	// Rodrigues rotation of V around a unit Axis by Angle (radians).
	inline FVec3 RotateAroundAxis(const FVec3& V, const FVec3& Axis, double Angle)
	{
		const double C = std::cos(Angle);
		const double S = std::sin(Angle);
		return V * C + Cross(Axis, V) * S + Axis * (Dot(Axis, V) * (1.0 - C));
	}

	// Rotate unit vector From toward unit vector To by at most MaxAngle radians.
	// FallbackAxis is used when From and To are exactly opposite.
	inline FVec3 RotateToward(const FVec3& From, const FVec3& To, double MaxAngle, const FVec3& FallbackAxis)
	{
		const double CosAngle = Clamp(Dot(From, To), -1.0, 1.0);
		const double Angle = std::acos(CosAngle);
		if (Angle <= MaxAngle)
		{
			return To;
		}
		const FVec3 Axis = Cross(From, To).GetSafeNormal(FallbackAxis);
		return RotateAroundAxis(From, Axis, MaxAngle).GetSafeNormal(From);
	}
}
