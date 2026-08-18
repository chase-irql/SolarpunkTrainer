# Third-party notices

Solarpunk Trainer includes or pins the following third-party software. Those
components remain under their respective licenses.

## Dear ImGui 1.92.8

Source: <https://github.com/ocornut/imgui>

The MIT License (MIT)

Copyright (c) 2014-2026 Omar Cornut

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

## MinHook

Source: <https://github.com/TsudaKageyu/minhook>

Copyright (C) 2009-2017 Tsuda Kageyu. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

The bundled Hacker Disassembler Engine 32/64 portions are Copyright (c)
2008-2009 Vyacheslav Patkov and are distributed under the same two-clause BSD
terms above.

## JSON for Modern C++ 3.11.3

Source: <https://github.com/nlohmann/json>

Pinned as the `third_party/json` Git submodule. Its MIT license is included in
that submodule as `LICENSE.MIT`.

## Inter variable font

Source: <https://github.com/rsms/inter>

Copyright (c) 2016 The Inter Project Authors. Distributed under the SIL Open
Font License 1.1; the complete license is included at
`SolarpunkTrainer/assets/fonts/LICENSE.txt`.

## Dumper-7 build dependency

Source: <https://github.com/Encryqed/Dumper-7>

The schema-probe build helper fetches the exact upstream commit
`3a849bb838422bea5cf417447d00a99549d932cf` into the ignored `.deps` directory.
Dumper-7 source is not vendored or sublicensed by this repository, and the
upstream project did not declare a license at the pinned revision. Review its
repository terms before redistributing a compiled schema probe. The
Solarpunk-specific patch and overlay under `schema_probe` remain covered by
this repository's MIT License.
