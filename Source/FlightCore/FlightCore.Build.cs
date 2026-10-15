// Engine-agnostic flight simulation (plain C++, no UObjects).
// The same sources are also built outside Unreal by Tools/FlightCoreTests (CMake + doctest).
using UnrealBuildTool;

public class FlightCore : ModuleRules
{
	public FlightCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
