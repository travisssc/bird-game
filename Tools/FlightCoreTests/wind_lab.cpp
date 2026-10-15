// Wind lab: renders the visible-wind streaks around a building to an SVG, using the real wind
// field and streak system, so the wind model can be eyeballed without the game.
//   ./wind_lab > docs/img/wind_streaks.svg
#include "FlightCore/WindField.h"
#include "FlightCore/WindStreaks.h"

#include <cmath>
#include <cstdio>

using namespace BirdFlight;

namespace
{
	constexpr double Dt = 1.0 / 60.0;
	constexpr int PanelW = 760, Margin = 20;

	struct FView
	{
		const char* Title;
		int Y0;
		int Height;
		double MinU, MaxU, MinV, MaxV;
		bool bSide; // side view: u = X, v = Z; top view: u = X, v = Y
	};

	double Px(const FView& V, double U) { return Margin + (U - V.MinU) / (V.MaxU - V.MinU) * PanelW; }
	double Py(const FView& V, double W) { return V.Y0 + V.Height - (W - V.MinV) / (V.MaxV - V.MinV) * V.Height; }

	void Panel(const FView& V, const FWindObstacle& B, const FWindStreakSystem* Systems, int NumSystems)
	{
		std::printf("<text x=\"%d\" y=\"%d\" font-size=\"14\" font-weight=\"600\" fill=\"#0f2a44\">%s</text>\n", Margin,
			V.Y0 - 8, V.Title);
		std::printf("<clipPath id=\"clip%d\"><rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" rx=\"6\"/></clipPath>\n",
			V.Y0, Margin, V.Y0, PanelW, V.Height);
		std::printf("<g clip-path=\"url(#clip%d)\">\n", V.Y0);
		std::printf("<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"url(#sky)\"/>\n", Margin, V.Y0,
			PanelW, V.Height);
		const double U0 = B.CenterX - B.HalfLengthX, U1 = B.CenterX + B.HalfLengthX;
		const double W0 = V.bSide ? B.BaseZ : B.CenterY - B.HalfWidthY;
		const double W1 = V.bSide ? B.BaseZ + B.Height : B.CenterY + B.HalfWidthY;
		std::printf("<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" fill=\"#5b6573\"/>\n", Px(V, U0),
			Py(V, W1), Px(V, U1) - Px(V, U0), Py(V, W0) - Py(V, W1));
		for (int i = 0; i < FWindStreakSystem::MaxStreaks * NumSystems; ++i)
		{
			const FWindStreak& S = Systems[i / FWindStreakSystem::MaxStreaks].GetStreak(i % FWindStreakSystem::MaxStreaks);
			if (!S.bAlive || S.NumPoints < 2 || S.Alpha <= 0.0)
			{
				continue;
			}
			// Side view shows a slice through the building's middle; top view a slice at mid height.
			const FVec3& H = S.Points[0];
			if (V.bSide ? std::fabs(H.Y) > 12.0 : (H.Z < 15.0 || H.Z > 38.0))
			{
				continue;
			}
			std::printf("<polyline fill=\"none\" stroke=\"#ffffff\" stroke-width=\"2\" stroke-linecap=\"round\" "
				"stroke-opacity=\"%.3f\" points=\"", std::fmin(S.Alpha * 4.0, 0.9));
			for (int p = 0; p < S.NumPoints; ++p)
			{
				const FVec3& Pt = S.Points[p];
				std::printf("%.1f,%.1f ", Px(V, Pt.X), Py(V, V.bSide ? Pt.Z : Pt.Y));
			}
			std::printf("\"/>\n");
		}
		std::printf("</g>\n");
	}
}

int main()
{
	FWindObstacle B;
	B.HalfLengthX = 15.0;
	B.HalfWidthY = 40.0;
	B.Height = 40.0;
	FWindParams W;
	W.BaseWind = FVec3(8.0, 0.0, 0.0);
	W.ShearExponent = 0.3;
	W.ShearRefHeightM = 40.0;
	W.Obstacles = &B;
	W.NumObstacles = 1;

	FWindStreakParams P;
	P.NumStreaks = FWindStreakSystem::MaxStreaks;
	P.SpawnRadiusM = 110.0;
	P.MinSpawnDistM = 0.0;
	P.ViewForwardBias = 0.0;
	P.LifetimeSec = 6.0;
	P.FadeSec = 0.3;
	P.TrailSampleSec = 0.25; // long trails read better at this zoom than the in-game default

	// Several systems (different seeds) with the viewer parked over the slice being drawn, so
	// the picture has enough streaks to show the flow.
	constexpr int Layers = 8;
	FWindStreakSystem Side[Layers] = { FWindStreakSystem(11), FWindStreakSystem(12), FWindStreakSystem(13),
		FWindStreakSystem(14), FWindStreakSystem(15), FWindStreakSystem(16), FWindStreakSystem(17), FWindStreakSystem(18) };
	FWindStreakSystem Top[Layers] = { FWindStreakSystem(21), FWindStreakSystem(22), FWindStreakSystem(23),
		FWindStreakSystem(24), FWindStreakSystem(25), FWindStreakSystem(26), FWindStreakSystem(27), FWindStreakSystem(28) };
	for (int L = 0; L < Layers; ++L)
	{
		for (int i = 0; i < 600; ++i)
		{
			Side[L].Update(W, i * Dt, FVec3(40.0, 0.0, 40.0), FVec3(1.0, 0.0, 0.0), P, Dt);
			Top[L].Update(W, i * Dt, FVec3(40.0, 0.0, 28.0), FVec3(1.0, 0.0, 0.0), P, Dt);
		}
	}

	const FView SideView{ "Side view: wind 8 m/s from the left, 40 m building. Streaks rise up the windward face; calm, tumbling wake behind",
		64, 300, -80.0, 160.0, 0.0, 95.0, true };
	const FView TopView{ "Top view (15-38 m up): air splits around the building; sheltered wake downwind",
		414, 570, -80.0, 160.0, -90.0, 90.0, false };
	std::printf("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" font-family=\"system-ui, sans-serif\">\n",
		PanelW + 2 * Margin, 414 + 570 + 20);
	std::printf("<defs><linearGradient id=\"sky\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#5f9fd6\"/>"
		"<stop offset=\"1\" stop-color=\"#a9cdeb\"/></linearGradient></defs>\n");
	std::printf("<rect width=\"100%%\" height=\"100%%\" fill=\"#ffffff\"/>\n");
	std::printf("<text x=\"%d\" y=\"22\" font-size=\"16\" font-weight=\"700\" fill=\"#0f2a44\">Wind lab: visible wind streaks (real wind field + streak system)</text>\n", Margin);
	Panel(SideView, B, Side, Layers);
	Panel(TopView, B, Top, Layers);
	std::printf("</svg>\n");
	return 0;
}
