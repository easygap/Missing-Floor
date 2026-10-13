using UnrealBuildTool;
using System.Collections.Generic;

public class IndieGameTarget : TargetRules
{
	public IndieGameTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("IndieGame");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Windows 파일 속성에도 배포 버전을 표시한다.
			BuildVersion = "0.9.1";
			WindowsPlatform.bSetResourceVersions = true;
		}
	}
}
