# hpack_benchmark

Benchmarks of [hpack](https://github.com/kelbon/hpack) compared with HPACK of [nghttp2](https://github.com/nghttp2/nghttp2) (`nghttp2_hd_*` API).

```bash
cmake . --preset=release
ninja -C build_release
./build_release/run_benchmark
```

What is measured (see benchmark.cpp):

* `bm_decode_*/browser_huffman` - decoding of requests encoded by nghttp2 (Huffman where its shorter, like browsers)
* `bm_decode_*/hidi_no_huffman` - decoding of requests encoded by hpack without Huffman (like hidi)
* `bm_encode_*` - encoding of server responses. `block_bytes` is average size of encoded block
* `bm_dyntab_*` - hpack dynamic table

`per_header` is time per one header.
