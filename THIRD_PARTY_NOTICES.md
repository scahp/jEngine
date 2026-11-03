# Third-party notices

This file restores the missing Dear ImGui notice and links the library-local
notices inspected on 2026-11-04. It is a partial notice index, not a complete
inventory of the repository's legacy libraries, binaries, or scene assets.

## Dear ImGui

The bundled `jEngine/External/IMGUI/imgui.h` reports `1.88 WIP`;
`imgui.cpp` refers to a `LICENSE.txt` that was missing from this checkout.
The following notice is restored from the official
[Dear ImGui v1.88 license](https://github.com/ocornut/imgui/blob/v1.88/LICENSE.txt).
The exact upstream commit of the bundled WIP snapshot has not been identified.

The MIT License (MIT)

Copyright (c) 2014-2022 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Existing library-local notices

These links preserve access to the existing notices; they do not replace their
full texts or establish the provenance of separately bundled binary builds.

| Library | Notices in this repository |
| --- | --- |
| spdlog (with bundled fmt headers) | [MIT license](jEngine/External/spdlog/LICENSE), [source and pinned commit](jEngine/External/spdlog/UPSTREAM.md); bundled fmt headers retain their upstream notices |
| tinyddsloader | [MIT license](jEngine/External/tinyddsloader/LICENCE), [source and pinned commit](jEngine/External/tinyddsloader/UPSTREAM.md) |
| EABase | [License](jEngine/External/EABase/LICENSE) |
| EASTL | [License](jEngine/External/EASTL/LICENSE), [third-party licenses](jEngine/External/EASTL/3RDPARTYLICENSES.TXT) |
| Tracy | [License](jEngine/External/tracy/LICENSE) |
| DirectX Shader Compiler source | [License](jEngine/External/DirectXShaderCompiler/LICENSE.TXT), [third-party notices](jEngine/External/DirectXShaderCompiler/ThirdPartyNotices.txt) |
| ShaderConductor source | [License](jEngine/External/ShaderConductor/LICENSE) |
| glslang | [License and component notices](jEngine/External/glslang/LICENSE.txt) |

tinyddsloader is recorded at commit
`a2ce2fdcef9c6c3687d1def2c8bb07ba27f4d1ba`; its local MIT notice was checked
against the [notice at that upstream commit](https://github.com/benikabocha/tinyddsloader/blob/a2ce2fdcef9c6c3687d1def2c8bb07ba27f4d1ba/LICENCE).
