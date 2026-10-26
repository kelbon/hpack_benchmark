// hpack (https://github.com/kelbon/hpack) vs nghttp2 HPACK (nghttp2_hd_* API)
//
// decode: both decode the same header blocks
//   browser_huffman: requests encoded by nghttp2 (Huffman for every string where its shorter, like browsers)
//   hidi_no_huffman: requests encoded by hpack without Huffman (like hidi)
// encode: responses of a server, hpack in several modes and nghttp2 (always uses Huffman where its shorter).
//   Counter 'block_bytes' is average size of encoded block: faster encoding is not better if it compresses
//   worse
//
// Counter 'per_header' is time per one header

#include <benchmark/benchmark.h>
#define NGHTTP2_NO_SSIZE_T
#include <hpack/hpack.hpp>
#include <nghttp2/nghttp2.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using bytes_t = std::vector<hpack::byte_t>;

struct header_t {
  std::string name;
  std::string value;
};
using headers_t = std::vector<header_t>;

// count of header blocks (requests / responses) processed in one iteration
constexpr int blocks_count = 100;

// typical browser request, :path and x-request-id are different in every request
headers_t request_headers(int n) {
  return {
      {":method", "GET"},
      {":scheme", "https"},
      {":authority", "api.example.com"},
      {":path",
       "/api/v1/users/" + std::to_string(12345 + n) + "/orders?limit=50&offset=" + std::to_string(n * 50)},
      {"user-agent",
       "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 "
       "Safari/537.36"},
      {"accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8"},
      {"accept-encoding", "gzip, deflate, br, zstd"},
      {"accept-language", "en-US,en;q=0.9"},
      {"cookie", "session=4f2a9c7e1b3d5f6a8c0e2b4d6f8a0c2e; theme=dark"},
      {"x-request-id", "b7e3c1a9-" + std::to_string(1000000 + n)},
  };
}

// typical response without :status (it is encoded separately, see encode benchmarks)
headers_t response_headers(int n) {
  return {
      {"content-type", "application/json"},
      {"content-length", std::to_string(1000 + n % 5000)},
      // date has precision of seconds, here a server sends 10 responses per second
      {"date", "Sun, 25 Oct 2026 18:00:" + std::to_string(10 + n / 10) + " GMT"},
      {"server", "hidi"},
      {"cache-control", "no-cache"},
      {"x-request-id", "b7e3c1a9-" + std::to_string(1000000 + n)},
  };
}

std::vector<headers_t> make_blocks(headers_t (*make)(int)) {
  std::vector<headers_t> blocks;
  for (int i = 0; i < blocks_count; ++i)
    blocks.push_back(make(i));
  return blocks;
}

size_t headers_count(const std::vector<headers_t>& blocks) {
  size_t n = 0;
  for (auto& b : blocks)
    n += b.size();
  return n;
}

// time per header in output
void set_per_header(benchmark::State& state, size_t headers) {
  state.counters["per_header"] = benchmark::Counter(
      double(headers), benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}

// ---------------- nghttp2 ----------------

struct ng_deflater {
  nghttp2_hd_deflater* p = nullptr;

  ng_deflater() {
    if (nghttp2_hd_deflate_new(&p, 4096) != 0)
      throw std::runtime_error("nghttp2_hd_deflate_new failed");
  }
  ng_deflater(ng_deflater&&) = delete;
  ~ng_deflater() {
    nghttp2_hd_deflate_del(p);
  }
};

struct ng_inflater {
  nghttp2_hd_inflater* p = nullptr;

  ng_inflater() {
    if (nghttp2_hd_inflate_new(&p) != 0)
      throw std::runtime_error("nghttp2_hd_inflate_new failed");
  }
  ng_inflater(ng_inflater&&) = delete;
  ~ng_inflater() {
    nghttp2_hd_inflate_del(p);
  }
};

std::vector<nghttp2_nv> to_nv(const headers_t& hdrs) {
  std::vector<nghttp2_nv> nv;
  for (auto& h : hdrs) {
    nv.push_back(nghttp2_nv{
        .name = (uint8_t*)h.name.data(),
        .value = (uint8_t*)h.value.data(),
        .namelen = h.name.size(),
        .valuelen = h.value.size(),
        .flags = NGHTTP2_NV_FLAG_NONE,
    });
  }
  return nv;
}

// appends encoded block to 'out'
void ng_encode(nghttp2_hd_deflater* d, const std::vector<nghttp2_nv>& nv, bytes_t& out) {
  size_t old_size = out.size();
  out.resize(old_size + nghttp2_hd_deflate_bound(d, nv.data(), nv.size()));
  nghttp2_ssize n =
      nghttp2_hd_deflate_hd2(d, out.data() + old_size, out.size() - old_size, nv.data(), nv.size());
  if (n < 0)
    throw std::runtime_error("nghttp2_hd_deflate_hd2 failed");
  out.resize(old_size + n);
}

// calls 'f(name, value)' for each header in block
template <typename F>
void ng_decode(nghttp2_hd_inflater* inf, const bytes_t& block, F&& f) {
  const uint8_t* in = block.data();
  size_t left = block.size();
  for (;;) {
    nghttp2_nv nv;
    int flags = 0;
    nghttp2_ssize n = nghttp2_hd_inflate_hd3(inf, &nv, &flags, in, left, /*in_final=*/1);
    if (n < 0)
      throw std::runtime_error("nghttp2_hd_inflate_hd3 failed");
    in += n;
    left -= n;
    if (flags & NGHTTP2_HD_INFLATE_EMIT)
      f(std::string_view((const char*)nv.name, nv.namelen),
        std::string_view((const char*)nv.value, nv.valuelen));
    if (flags & NGHTTP2_HD_INFLATE_FINAL) {
      nghttp2_hd_inflate_end_headers(inf);
      return;
    }
  }
}

// ---------------- hpack ----------------

// calls 'f(name, value)' for each header in block
template <typename F>
void hpack_decode(hpack::decoder& dec, const bytes_t& block, F&& f) {
  const hpack::byte_t* in = block.data();
  const hpack::byte_t* e = block.data() + block.size();
  hpack::header_view h;
  while (in != e) {
    dec.decode_header(in, e, h);
    if (h)  // may be dynamic table size update
      f(h.name.str(), h.value.str());
  }
}

// ---------------- data ----------------

const std::vector<headers_t>& requests() {
  static const std::vector<headers_t> r = make_blocks(request_headers);
  return r;
}

const std::vector<headers_t>& responses() {
  static const std::vector<headers_t> r = make_blocks(response_headers);
  return r;
}

std::vector<bytes_t> encode_requests_nghttp2() {
  ng_deflater d;
  std::vector<bytes_t> blocks;
  for (auto& hdrs : requests())
    ng_encode(d.p, to_nv(hdrs), blocks.emplace_back());
  return blocks;
}

// like hidi client: pseudoheaders with static table, other headers with encode_auto
std::vector<bytes_t> encode_requests_hpack() {
  hpack::encoder enc;
  std::vector<bytes_t> blocks;
  for (auto& hdrs : requests()) {
    auto out = std::back_inserter(blocks.emplace_back());
    for (auto& h : hdrs) {
      if (h.name.starts_with(':'))
        enc.encode</*Cache=*/true>(h.name, h.value, out);
      else
        enc.encode_auto(h.name, h.value, out);
    }
  }
  return blocks;
}

// checks that both implementations decode 'blocks' into requests()
void check_decode(const std::vector<bytes_t>& blocks) {
  ng_inflater inf;
  hpack::decoder dec;
  for (size_t i = 0; i < blocks.size(); ++i) {
    headers_t ng, hp;
    ng_decode(inf.p, blocks[i], [&](std::string_view n, std::string_view v) {
      ng.push_back({std::string(n), std::string(v)});
    });
    hpack_decode(dec, blocks[i], [&](std::string_view n, std::string_view v) {
      hp.push_back({std::string(n), std::string(v)});
    });
    auto eq = [](const headers_t& a, const headers_t& b) {
      return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](auto& x, auto& y) {
               return x.name == y.name && x.value == y.value;
             });
    };
    if (!eq(ng, requests()[i]) || !eq(hp, requests()[i]))
      throw std::runtime_error("decoded headers do not match");
  }
}

const std::vector<bytes_t>& request_blocks(std::string_view dataset) {
  static const std::vector<bytes_t> browser = [] {
    auto b = encode_requests_nghttp2();
    check_decode(b);
    return b;
  }();
  static const std::vector<bytes_t> hidi = [] {
    auto b = encode_requests_hpack();
    check_decode(b);
    return b;
  }();
  return dataset == "browser_huffman" ? browser : hidi;
}

// ---------------- decode benchmarks ----------------

void bm_decode_hpack(benchmark::State& state, std::string_view dataset) {
  const auto& blocks = request_blocks(dataset);
  size_t sink = 0;
  for (auto _ : state) {
    hpack::decoder dec;
    for (auto& b : blocks)
      hpack_decode(dec, b, [&](std::string_view n, std::string_view v) { sink += n.size() + v.size(); });
  }
  benchmark::DoNotOptimize(sink);
  set_per_header(state, headers_count(requests()));
}

void bm_decode_nghttp2(benchmark::State& state, std::string_view dataset) {
  const auto& blocks = request_blocks(dataset);
  size_t sink = 0;
  for (auto _ : state) {
    ng_inflater inf;
    for (auto& b : blocks)
      ng_decode(inf.p, b, [&](std::string_view n, std::string_view v) { sink += n.size() + v.size(); });
  }
  benchmark::DoNotOptimize(sink);
  set_per_header(state, headers_count(requests()));
}

BENCHMARK_CAPTURE(bm_decode_hpack, browser_huffman, "browser_huffman");
BENCHMARK_CAPTURE(bm_decode_nghttp2, browser_huffman, "browser_huffman");
BENCHMARK_CAPTURE(bm_decode_hpack, hidi_no_huffman, "hidi_no_huffman");
BENCHMARK_CAPTURE(bm_decode_nghttp2, hidi_no_huffman, "hidi_no_huffman");

// ---------------- encode benchmarks ----------------

enum class hpack_mode { encode_auto, encode_auto_huffman, encode_with_cache };

// encodes responses like hidi server: :status 200 and headers
void bm_encode_hpack(benchmark::State& state, hpack_mode mode) {
  const auto& rsps = responses();
  bytes_t buf;
  buf.reserve(4096);
  size_t total_bytes = 0;
  for (auto _ : state) {
    hpack::encoder enc;
    for (auto& hdrs : rsps) {
      buf.clear();
      auto out = std::back_inserter(buf);
      enc.encode_status(200, out);
      for (auto& h : hdrs) {
        switch (mode) {
          case hpack_mode::encode_auto:
            enc.encode_auto(h.name, h.value, out);
            break;
          case hpack_mode::encode_auto_huffman:
            enc.encode_auto</*Huffman=*/true>(h.name, h.value, out);
            break;
          case hpack_mode::encode_with_cache:
            enc.encode_with_cache(h.name, h.value, out);
            break;
        }
      }
      total_bytes += buf.size();
    }
  }
  set_per_header(state, headers_count(rsps) + rsps.size());  // + :status
  state.counters["block_bytes"] = double(total_bytes) / (double(state.iterations()) * rsps.size());
}

void bm_encode_nghttp2(benchmark::State& state) {
  const auto& rsps = responses();
  std::vector<headers_t> with_status;
  for (auto& hdrs : rsps) {
    headers_t& h = with_status.emplace_back(hdrs);
    h.insert(h.begin(), {":status", "200"});
  }
  // points into 'with_status'
  std::vector<std::vector<nghttp2_nv>> nvs;
  for (auto& hdrs : with_status)
    nvs.push_back(to_nv(hdrs));

  bytes_t buf;
  buf.reserve(4096);
  size_t total_bytes = 0;
  for (auto _ : state) {
    ng_deflater d;
    for (auto& nv : nvs) {
      buf.clear();
      ng_encode(d.p, nv, buf);
      total_bytes += buf.size();
    }
  }
  set_per_header(state, headers_count(rsps) + rsps.size());
  state.counters["block_bytes"] = double(total_bytes) / (double(state.iterations()) * rsps.size());
}

BENCHMARK_CAPTURE(bm_encode_hpack, encode_auto, hpack_mode::encode_auto);
BENCHMARK_CAPTURE(bm_encode_hpack, encode_auto_huffman, hpack_mode::encode_auto_huffman);
BENCHMARK_CAPTURE(bm_encode_hpack, encode_with_cache, hpack_mode::encode_with_cache);
BENCHMARK(bm_encode_nghttp2);

// ---------------- hpack dynamic table ----------------

std::vector<header_t> table_entries() {
  std::vector<header_t> e;
  for (int i = 0; i < 1000; ++i)
    for (auto& h : request_headers(i))
      e.push_back(h);
  return e;
}

void bm_dyntab_add_entry(benchmark::State& state) {
  const auto entries = table_entries();
  hpack::dynamic_table_t table(4096);
  for (auto& h : entries)  // fill table, then every add_entry evicts
    table.add_entry(h.name, h.value);
  for (auto _ : state)
    for (auto& h : entries)
      benchmark::DoNotOptimize(table.add_entry(h.name, h.value));
  state.SetItemsProcessed(state.iterations() * entries.size());
}

void bm_dyntab_find(benchmark::State& state) {
  const auto entries = table_entries();
  hpack::dynamic_table_t table(4096);
  for (auto& h : entries)
    table.add_entry(h.name, h.value);
  for (auto _ : state)
    for (auto& h : entries)
      benchmark::DoNotOptimize(table.find(h.name, h.value));
  state.SetItemsProcessed(state.iterations() * entries.size());
}

BENCHMARK(bm_dyntab_add_entry);
BENCHMARK(bm_dyntab_find);

}  // namespace

BENCHMARK_MAIN();
