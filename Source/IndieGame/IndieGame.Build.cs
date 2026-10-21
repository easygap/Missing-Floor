using UnrealBuildTool;
using System.IO;

public class IndieGame : ModuleRules
{
	public IndieGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// Keep feature folders directly under the module root while exposing the
		// same stable include paths to every translation unit.
		PublicIncludePaths.Add(ModuleDirectory);
		PrivateIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"AudioExtensions",
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayTags",
			// Runtime composite font (Korean HUD text) uses SlateCore types.
			"SlateCore",
			// Photo-prop meshes are resolved by path at runtime.
			"AssetRegistry"
		});

		// Optional microphone mode reduces capture buffers to a local envelope;
		// the platform backend is loaded by the AudioCapture project plugin.
		PrivateDependencyModuleNames.Add("AudioCaptureCore");
		// 연출 검사에서 실제 게임 믹서 출력을 녹음한다.
		PrivateDependencyModuleNames.Add("AudioMixer");
		// 배포본의 명시적인 성능 검사에서 GPU 시간과 메모리를 읽는다.
		PrivateDependencyModuleNames.Add("RHI");
		PrivateDependencyModuleNames.Add("RenderCore");
		PrivateDependencyModuleNames.Add("Json");
		// 소품 접지 검사에서 복잡 충돌이 실제 삼각형을 사용하는지 확인한다.
		PrivateDependencyModuleNames.Add("PhysicsCore");
		// 소품 배치 검사는 에디터에서만 메시 원본 삼각형을 읽는다(Sequence/IGPlacementAudit).
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "MeshDescription", "StaticMeshDescription" });
		}

		// Bundle UI typefaces with every target. Loading from the project
		// directory keeps glyph metrics identical in Editor and packaged builds.
		string FontsDirectory = Path.Combine(ModuleDirectory, "UI", "Fonts");
		string[] BundledFontFiles =
		{
			"Pretendard-Regular.otf",
			"Pretendard-SemiBold.otf",
			"GowunBatang-Bold.ttf",
			"OFL-Pretendard.txt",
			"OFL-GowunBatang.txt",
			// 일본어·중국어 간체·번체 지역 서브셋. 윈도우에 깔린 글꼴에 기대지 않는다.
			"NotoSansJP-Regular.otf",
			"NotoSansSC-Regular.otf",
			"NotoSansTC-Regular.otf",
			"OFL-NotoSansCJK.txt"
		};
		foreach (string FontFile in BundledFontFiles)
		{
			RuntimeDependencies.Add(
				"$(TargetOutputDir)/UI/Fonts/" + FontFile,
				Path.Combine(FontsDirectory, FontFile),
				StagedFileType.NonUFS);
		}
	}
}
