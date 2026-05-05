This directory gets additional Kotlin sources as the API fills out. The
`expect object LilNativeDecoder` above needs an `actual` in every platform
source set before the module compiles — those land in A3.3/A3.4/A3.5/A3.6.
