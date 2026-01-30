// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class LBRuntimeRecorder : ModuleRules
{
    public LBRuntimeRecorder(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "./ThirdParty"));
        string FFmpegPath = Path.Combine(ThirdPartyPath, "ffmpeg");

        PublicIncludePaths.Add(Path.Combine(FFmpegPath, "include"));

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string LibPath = Path.Combine(FFmpegPath, "lib");

            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "avcodec.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "avformat.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "avutil.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "swscale.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "swresample.lib"));

            string BinPath = Path.Combine(FFmpegPath, "bin");


            // ����ʱDLL
            string[] DynamicLibraries = Directory.GetFiles(BinPath, "*.dll");

            foreach (string item in DynamicLibraries)
            {

                string DynamicLibraryName = Path.GetFileName(item);
                System.Console.WriteLine(item);

                RuntimeDependencies.Add(Path.Combine("$(BinaryOutputDir)", DynamicLibraryName), item);
            }

            //RuntimeDependencies.Add("$(TargetOutputDir)/avcodec-62.dll", Path.Combine(BinPath, "avcodec-62.dll"), StagedFileType.NonUFS);
            //RuntimeDependencies.Add("$(TargetOutputDir)/avformat-62.dll", Path.Combine(BinPath, "avformat-62.dll"), StagedFileType.NonUFS);
            //RuntimeDependencies.Add("$(TargetOutputDir)/avutil-60.dll", Path.Combine(BinPath, "avutil-60.dll"), StagedFileType.NonUFS);
            //RuntimeDependencies.Add("$(TargetOutputDir)/swscale-9.dll", Path.Combine(BinPath, "swscale-9.dll"),StagedFileType.NonUFS);
            //RuntimeDependencies.Add("$(TargetOutputDir)/swresample-6.dll", Path.Combine(BinPath, "swresample-6.dll"), StagedFileType.NonUFS);
        }


        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
				// ... add other public dependencies that you statically link with here ...
			}
            );


        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "RenderCore",
                "RHI",
                "Renderer",
				// ... add private dependencies that you statically link with here ...	
			}
            );


        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
				// ... add any modules that your module loads dynamically here ...
			}
            );
    }
}
