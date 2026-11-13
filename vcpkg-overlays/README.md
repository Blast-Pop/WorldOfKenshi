# vcpkg overlay ports

`gamenetworkingsockets`: upstream vcpkg port (MIT, microsoft/vcpkg @ 2ec0f17) plus one fix — pass
`-DProtobuf_USE_STATIC_LIBS=ON` for static triplets, otherwise the static library is compiled
with `PROTOBUF_USE_DLLS` and fails to link against static protobuf.
