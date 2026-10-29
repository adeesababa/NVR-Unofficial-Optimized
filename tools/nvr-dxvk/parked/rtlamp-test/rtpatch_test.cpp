#include "dxvk_nvr_rtpatch.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
int main(int argc, char** argv) {
  if (argc < 3) return 2;
  std::ifstream in(argv[1], std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<uint32_t> code(bytes.size() / 4);
  memcpy(code.data(), bytes.data(), code.size() * 4);
  size_t before = code.size();
  uint32_t n = dxvk::nvrPatchCubeGradRayQueries(code, 0x123456789ABCDEF0ull);
  std::ofstream out(argv[2], std::ios::binary);
  out.write(reinterpret_cast<const char*>(code.data()), code.size() * 4);
  printf("replaced %u marked samples, %zu -> %zu words\n", n, before, code.size());
  if (argc > 3 && n != (uint32_t)atoi(argv[3])) { printf("FAIL: expected %s\n", argv[3]); return 1; }
  return 0;
}
