// sdk/unity/com.djinn.scc/Editor/SCCAsset.cs
//
// ScriptableObject wrapper for an imported .scc test fixture.

using UnityEngine;

namespace Djinn.SCC.Editor
{
    /// <summary>
    /// Imported representation of a .scc binary asset (raw SEI payload
    /// bytes). Created by <see cref="SCCImporter"/>; consumed by tests
    /// or runtime code via <c>AssetDatabase.LoadAssetAtPath</c>.
    /// </summary>
    public sealed class SCCAsset : ScriptableObject
    {
        [SerializeField, HideInInspector]
        internal byte[] m_SeiBytes = System.Array.Empty<byte>();

        /// <summary>The SEI payload bytes loaded from the .scc file.</summary>
        public byte[] SeiBytes => m_SeiBytes;
    }
}
