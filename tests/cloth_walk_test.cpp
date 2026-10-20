#include "../src/core/ClothWalk.h"
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
	if (argc < 3) { printf("usage: cloth_walk_test <in> <out>\n"); return 2; }
	FILE* f = nullptr;
	if (fopen_s(&f, argv[1], "rb") || !f) return 2;
	uint32_t n = 0;
	fread(&n, 4, 1, f);
	std::vector<float> pos(n * 3), w(n);
	std::vector<uint8_t> lab(n);
	float c[2], hipZ;
	fread(pos.data(), 4, pos.size(), f); fread(w.data(), 4, n, f); fread(lab.data(), 1, n, f); fread(c, 4, 2, f); fread(&hipZ, 4, 1, f);
	ClothWalk::Ring ring;
	if (!ClothWalk::BuildRing(pos.data(), n, w.data(), lab.data(), c, hipZ, ring)) { printf("no ring\n"); return 1; }
	uint32_t frames = 0;
	fread(&frames, 4, 1, f);
	FILE* o = nullptr;
	if (fopen_s(&o, argv[2], "wb") || !o) return 2;
	fwrite(&n, 4, 1, o); fwrite(ring.u.data(), 4, n, o); fwrite(ring.swing.data(), 4, n, o);
	fwrite(ring.p, 8, 48, o); fwrite(ring.a, 8, 48, o); fwrite(ring.L, 8, 16, o);
	ClothWalk::State st; ClothWalk::Params prm;
	for (uint32_t i = 0; i < frames; i++) {
		double dt, r[9], t[3], legs[6][4], out[16][3];
		fread(&dt, 8, 1, f); fread(r, 8, 9, f); fread(t, 8, 3, f); fread(legs, 8, 24, f);
		ClothWalk::Xf x;
		for (int a = 0; a < 3; a++) { for (int b = 0; b < 3; b++) x.r[a][b] = r[a * 3 + b]; x.t[a] = t[a]; }
		ClothWalk::Step(st, ring, x, legs, dt, prm, out);
		fwrite(out, 8, 48, o);
	}
	fclose(f); fclose(o);
	printf("ok: %u vertices, %u frames\n", n, frames);
	return 0;
}
