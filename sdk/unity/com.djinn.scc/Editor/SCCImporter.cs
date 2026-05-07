// sdk/unity/com.djinn.scc/Editor/SCCImporter.cs
//
// ScriptedImporter for .scc test-fixture files.
//
// Drop a .scc file into a Unity project; this importer wraps the bytes
// in an SCCAsset so test scripts can access the payload via the asset
// database.

using System.IO;
using UnityEditor.AssetImporters;
using UnityEngine;

namespace Djinn.SCC.Editor
{
    [ScriptedImporter(version: 1, ext: "scc")]
    public sealed class SCCImporter : ScriptedImporter
    {
        public override void OnImportAsset(AssetImportContext ctx)
        {
            byte[] bytes = File.ReadAllBytes(ctx.assetPath);
            var asset = ScriptableObject.CreateInstance<SCCAsset>();
            // Use reflection-free assignment via the internal field.
            // (SCCAsset.m_SeiBytes is internal in the same assembly.)
            typeof(SCCAsset)
                .GetField("m_SeiBytes",
                    System.Reflection.BindingFlags.Instance |
                    System.Reflection.BindingFlags.NonPublic)!
                .SetValue(asset, bytes);
            ctx.AddObjectToAsset("main", asset);
            ctx.SetMainObject(asset);
        }
    }
}
