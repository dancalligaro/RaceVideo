# Third-Party Notices

RaceVideo depends on the following third-party software. These components
remain under their respective licenses; the Apache-2.0 license for RaceVideo
does not replace those licenses.

## Abseil

Copyright Google LLC and contributors.

Licensed under the Apache License, Version 2.0. A copy of that license is
included in the repository's `LICENSE` file.

Source: <https://github.com/abseil/abseil-cpp>

## GoPro GPMF parser

Copyright 2016-2020 GoPro, Inc.

The parser is offered under either Apache-2.0 or MIT. RaceVideo uses it under
the Apache License, Version 2.0. A copy is included in `LICENSE`.

Source: <https://github.com/gopro/gpmf-parser>

GoPro is a trademark of GoPro, Inc. Use of the parser and references to GoPro
describe compatibility only and do not imply affiliation or endorsement.

## GoogleTest

GoogleTest is used only for development and testing.

Copyright 2008, Google Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
* Neither the name of Google Inc. nor the names of its contributors may be
  used to endorse or promote products derived from this software without
  specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Source: <https://github.com/google/googletest>

## Protocol Buffers

Copyright 2008 Google Inc. All rights reserved.

Protocol Buffers is distributed under the BSD 3-Clause license. Source and
license text: <https://github.com/protocolbuffers/protobuf>

The generated C++ files are produced from RaceVideo's own `.proto` schema;
the Protocol Buffers runtime retains its upstream license.

## External FFmpeg dependency

FFmpeg is not included in or distributed with RaceVideo. It is a user-supplied
external executable that future RaceVideo versions will invoke as a separate
process. RaceVideo release artifacts must not bundle, download, install, or
redistribute FFmpeg.

The user's FFmpeg build remains separately licensed. FFmpeg is generally
LGPL-2.1-or-later, but optional components can make a particular build GPL or
non-redistributable. Users and downstream packagers are responsible for the
license terms applicable to the FFmpeg build they obtain independently.

Source: <https://ffmpeg.org/>

## stb_image_write

RaceVideo uses `stb_image_write.h` to write debug PNG frames. It is available
under the MIT License or the public domain; RaceVideo uses it under the MIT
License.

Copyright Sean Barrett and contributors.

Source and license: <https://github.com/nothings/stb>
