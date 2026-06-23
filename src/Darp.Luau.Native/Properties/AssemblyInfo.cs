using System.Runtime.CompilerServices;

[assembly: DisableRuntimeMarshalling]

#if NETSTANDARD2_0
namespace System.Runtime.CompilerServices
{
    [AttributeUsage(AttributeTargets.Assembly)]
    internal sealed class DisableRuntimeMarshallingAttribute : Attribute;
}
#endif