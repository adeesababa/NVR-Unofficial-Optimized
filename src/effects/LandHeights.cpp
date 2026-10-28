#include "LandHeights.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <memory>
#include <unordered_map>


namespace {

struct Bits {
	const uint8_t*	p;
	const uint8_t*	end;
	uint32_t		buffer = 0;
	int				count = 0;

	void Fill() { while (count <= 24 && p < end) { buffer |= (uint32_t)*p++ << count; count += 8; } }
	bool Need(int n) { if (count < n) Fill(); return count >= n; }
	uint32_t Take(int n) { uint32_t v = buffer & ((1u << n) - 1); buffer >>= n; count -= n; return v; }
};

struct Huffman {
	static const int FastBits = 9;
	uint16_t	fast[1 << FastBits];
	uint16_t	counts[16];
	uint16_t	symbols[288];

	bool Build(const uint8_t* lengths, int n) {
		memset(counts, 0, sizeof(counts));
		memset(fast, 0, sizeof(fast));
		for (int s = 0; s < n; s++) counts[lengths[s]]++;
		counts[0] = 0;
		uint16_t offsets[16] = {};
		for (int len = 1; len < 16; len++) offsets[len] = offsets[len - 1] + counts[len - 1];
		for (int s = 0; s < n; s++) if (lengths[s]) symbols[offsets[lengths[s]]++] = (uint16_t)s;
		int code = 0, next[16] = {};
		for (int len = 1; len < 16; len++) { code = (code + counts[len - 1]) << 1; next[len] = code; }
		for (int s = 0; s < n; s++) {
			const int len = lengths[s];
			if (!len || len > FastBits) continue;
			int c = next[len]++, reversed = 0;
			for (int i = 0; i < len; i++) { reversed = (reversed << 1) | (c & 1); c >>= 1; }
			for (int fill = reversed; fill < (1 << FastBits); fill += 1 << len) fast[fill] = (uint16_t)((s << 4) | len);
		}
		return true;
	}

	int Decode(Bits& bits) const {
		bits.Fill();
		const uint16_t entry = fast[bits.buffer & ((1 << FastBits) - 1)];
		if (entry) {
			const int len = entry & 15;
			if (bits.count < len) return -1;
			bits.Take(len);
			return entry >> 4;
		}
		int code = 0, first = 0, index = 0;
		for (int len = 1; len < 16; len++) {
			if (!bits.Need(1)) return -1;
			code |= (int)bits.Take(1);
			const int count = counts[len];
			if (code - count < first) return symbols[index + (code - first)];
			index += count;
			first += count;
			first <<= 1;
			code <<= 1;
		}
		return -1;
	}
};

const uint16_t LengthBase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
const uint8_t LengthExtra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
const uint16_t DistanceBase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
const uint8_t DistanceExtra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

int InflateBlock(Bits& bits, const Huffman& literals, const Huffman& distances, std::vector<uint8_t>& out, size_t limit) {
	for (;;) {
		const int symbol = literals.Decode(bits);
		if (symbol < 0) return -1;
		if (symbol < 256) { out.push_back((uint8_t)symbol); }
		else if (symbol == 256) return 1;
		else {
			const int l = symbol - 257;
			if (l >= 29 || !bits.Need(LengthExtra[l])) return -1;
			const size_t length = LengthBase[l] + bits.Take(LengthExtra[l]);
			const int d = distances.Decode(bits);
			if (d < 0 || d >= 30 || !bits.Need(DistanceExtra[d])) return -1;
			const size_t distance = DistanceBase[d] + bits.Take(DistanceExtra[d]);
			if (distance > out.size()) return -1;
			const size_t from = out.size() - distance;
			for (size_t i = 0; i < length; i++) out.push_back(out[from + i]);
		}
		if (limit && out.size() >= limit) return 0;
	}
}

}

bool LandHeights::Inflate(const uint8_t* In, size_t InSize, std::vector<uint8_t>& Out, size_t Limit) {
	if (InSize < 2) return false;
	const uint8_t cmf = In[0], flg = In[1];
	if ((cmf & 15) != 8 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20)) return false;
	Bits bits{ In + 2, In + InSize };
	static Huffman fixedLiterals, fixedDistances;
	static bool fixedBuilt = false;
	if (!fixedBuilt) {
		uint8_t lengths[288];
		for (int i = 0; i < 288; i++) lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
		fixedLiterals.Build(lengths, 288);
		for (int i = 0; i < 30; i++) lengths[i] = 5;
		fixedDistances.Build(lengths, 30);
		fixedBuilt = true;
	}
	Huffman literals, distances;
	for (;;) {
		if (!bits.Need(3)) return false;
		const bool last = bits.Take(1) != 0;
		const uint32_t type = bits.Take(2);
		int result;
		if (type == 0) {
			bits.Take(bits.count & 7);
			if (!bits.Need(16)) return false;
			const uint32_t length = bits.Take(16);
			if (!bits.Need(16)) return false;
			const uint32_t inverse = bits.Take(16);
			if ((length ^ 0xFFFF) != inverse) return false;
			uint32_t left = length;
			while (left && bits.count >= 8) { Out.push_back((uint8_t)bits.Take(8)); left--; }
			if ((size_t)(bits.end - bits.p) < left) return false;
			Out.insert(Out.end(), bits.p, bits.p + left);
			bits.p += left;
			result = Limit && Out.size() >= Limit ? 0 : 1;
		}
		else if (type == 1) result = InflateBlock(bits, fixedLiterals, fixedDistances, Out, Limit);
		else if (type == 2) {
			if (!bits.Need(14)) return false;
			const int literalCount = (int)bits.Take(5) + 257, distanceCount = (int)bits.Take(5) + 1, codeCount = (int)bits.Take(4) + 4;
			static const uint8_t order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
			uint8_t codeLengths[19] = {};
			for (int i = 0; i < codeCount; i++) { if (!bits.Need(3)) return false; codeLengths[order[i]] = (uint8_t)bits.Take(3); }
			Huffman codes;
			codes.Build(codeLengths, 19);
			uint8_t lengths[320] = {};
			for (int i = 0; i < literalCount + distanceCount;) {
				const int symbol = codes.Decode(bits);
				if (symbol < 0) return false;
				if (symbol < 16) { lengths[i++] = (uint8_t)symbol; continue; }
				int repeat = 0;
				uint8_t value = 0;
				if (symbol == 16) { if (!i || !bits.Need(2)) return false; value = lengths[i - 1]; repeat = 3 + (int)bits.Take(2); }
				else if (symbol == 17) { if (!bits.Need(3)) return false; repeat = 3 + (int)bits.Take(3); }
				else { if (!bits.Need(7)) return false; repeat = 11 + (int)bits.Take(7); }
				if (i + repeat > literalCount + distanceCount) return false;
				while (repeat--) lengths[i++] = value;
			}
			literals.Build(lengths, literalCount);
			distances.Build(lengths + literalCount, distanceCount);
			result = InflateBlock(bits, literals, distances, Out, Limit);
		}
		else return false;
		if (result < 0) return false;
		if (result == 0 || last) return true;
	}
}

namespace {

const uint32_t RecordCompressed = 0x00040000, RecordDeleted = 0x00000020;

uint32_t Tag(const char* s) { return (uint32_t)(uint8_t)s[0] | ((uint32_t)(uint8_t)s[1] << 8) | ((uint32_t)(uint8_t)s[2] << 16) | ((uint32_t)(uint8_t)s[3] << 24); }

struct Header {
	uint32_t	type, size, label, groupType;
};

class Reader {
public:
	explicit Reader(const std::string& Path) {
		if (fopen_s(&file, Path.c_str(), "rb") || !file) { file = nullptr; return; }
		setvbuf(file, nullptr, _IONBF, 0);
		_fseeki64(file, 0, SEEK_END);
		size = (uint64_t)_ftelli64(file);
		buffer.resize(1 << 18);
	}
	~Reader() { if (file) fclose(file); }
	bool Ok() const { return file != nullptr; }
	uint64_t Size() const { return size; }

	bool Read(uint64_t pos, void* dst, size_t n) {
		if (pos + n > size) return false;
		if (pos >= start && pos + n <= start + length) { memcpy(dst, &buffer[(size_t)(pos - start)], n); return true; }
		if (n > buffer.size()) {
			if (_fseeki64(file, (long long)pos, SEEK_SET)) return false;
			return fread(dst, 1, n, file) == n;
		}
		if (_fseeki64(file, (long long)pos, SEEK_SET)) return false;
		length = fread(buffer.data(), 1, buffer.size(), file);
		start = pos;
		if (length < n) return false;
		memcpy(dst, buffer.data(), n);
		return true;
	}

	bool ReadHeader(uint64_t pos, Header& h) {
		uint32_t raw[6];
		if (!Read(pos, raw, 24)) return false;
		h.type = raw[0]; h.size = raw[1]; h.label = raw[2]; h.groupType = raw[3];
		return true;
	}

private:
	FILE*		file = nullptr;
	uint64_t	size = 0;
	std::vector<uint8_t>	buffer;
	uint64_t	start = 0;
	size_t		length = 0;
};

bool SameName(const std::string& a, const std::string& b) {
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
	return true;
}

bool RecordData(Reader& file, uint64_t pos, const Header& h, std::vector<uint8_t>& raw, std::vector<uint8_t>& data, size_t limit) {
	raw.resize(h.size);
	if (h.size && !file.Read(pos + 24, raw.data(), h.size)) return false;
	if (!(h.label & RecordCompressed)) { data.swap(raw); return true; }
	if (h.size < 4) return false;
	uint32_t unpacked;
	memcpy(&unpacked, raw.data(), 4);
	data.clear();
	data.reserve(limit ? (std::min)((size_t)unpacked, limit + 512) : unpacked);
	return LandHeights::Inflate(raw.data() + 4, h.size - 4, data, limit);
}

const uint8_t* FindSub(const std::vector<uint8_t>& data, uint32_t type, uint32_t& size) {
	size_t p = 0;
	uint32_t longSize = 0;
	while (p + 6 <= data.size()) {
		uint32_t t; uint16_t s;
		memcpy(&t, &data[p], 4);
		memcpy(&s, &data[p + 4], 2);
		p += 6;
		uint32_t n = s;
		if (longSize) { n = longSize; longSize = 0; }
		if (t == Tag("XXXX") && s == 4 && p + 4 <= data.size()) { memcpy(&longSize, &data[p], 4); p += 4; continue; }
		if (p + n > data.size()) return nullptr;
		if (t == type) { size = n; return &data[p]; }
		p += n;
	}
	return nullptr;
}

struct LandEntry {
	int			x, y;
	uint32_t	plugin;
	uint64_t	pos;
	Header		header;
};

struct Walker {
	Reader&			file;
	uint32_t		pluginIndex;
	std::string		pluginName;
	std::vector<std::string>	masters;
	const std::string&	worldFile;
	uint32_t		worldID;
	std::unordered_map<int64_t, LandEntry>&	lands;
	std::vector<uint8_t>	raw, data;

	bool IsWorld(uint32_t formID) const {
		const uint32_t index = formID >> 24;
		const std::string& owner = index < masters.size() ? masters[index] : pluginName;
		return (formID & 0xFFFFFF) == worldID && SameName(owner, worldFile);
	}

	bool CellGrid(uint64_t pos, const Header& h, int& x, int& y) {
		if (!RecordData(file, pos, h, raw, data, 0)) return false;
		uint32_t size = 0;
		const uint8_t* xclc = FindSub(data, Tag("XCLC"), size);
		if (!xclc || size < 8) return false;
		int32_t v[2];
		memcpy(v, xclc, 8);
		x = v[0]; y = v[1];
		return true;
	}

	void Children(uint64_t pos, uint64_t end, bool haveCell, int cellX, int cellY, uint32_t cellID) {
		while (pos + 24 <= end) {
			Header h;
			if (!file.ReadHeader(pos, h)) return;
			if (h.type == Tag("GRUP")) {
				if (h.size < 24) return;
				const int32_t type = (int32_t)h.groupType;
				if (type == 4 || type == 5) Children(pos + 24, pos + h.size, false, 0, 0, 0);
				else if (type == 6 && haveCell && h.label == cellID) Children(pos + 24, pos + h.size, true, cellX, cellY, cellID);
				else if (type == 9 && haveCell) Children(pos + 24, pos + h.size, true, cellX, cellY, cellID);
				pos += h.size;
				continue;
			}
			if (h.type == Tag("CELL")) {
				cellID = h.groupType;
				haveCell = !(h.label & RecordDeleted) && CellGrid(pos, h, cellX, cellY);
			}
			else if (h.type == Tag("LAND") && haveCell) {
				const int64_t key = ((int64_t)cellX << 32) | (uint32_t)cellY;
				if (h.label & RecordDeleted) lands.erase(key);
				else lands[key] = LandEntry{ cellX, cellY, pluginIndex, pos, h };
			}
			pos += 24 + (uint64_t)h.size;
		}
	}

	bool Run(std::string& error) {
		Header h;
		if (!file.ReadHeader(0, h) || h.type != Tag("TES4")) { error = "not a plugin"; return false; }
		if (!RecordData(file, 0, h, raw, data, 0)) { error = "unreadable header"; return false; }
		for (size_t p = 0; p + 6 <= data.size();) {
			uint32_t t; uint16_t s;
			memcpy(&t, &data[p], 4);
			memcpy(&s, &data[p + 4], 2);
			p += 6;
			if (p + s > data.size()) break;
			if (t == Tag("MAST")) masters.emplace_back((const char*)&data[p], strnlen((const char*)&data[p], s));
			p += s;
		}
		uint64_t pos = 24 + (uint64_t)h.size;
		while (pos + 24 <= file.Size()) {
			if (!file.ReadHeader(pos, h) || h.type != Tag("GRUP") || h.size < 24) break;
			if (h.groupType == 0 && h.label == Tag("WRLD")) {
				for (uint64_t p = pos + 24, end = pos + h.size; p + 24 <= end;) {
					Header w;
					if (!file.ReadHeader(p, w)) break;
					if (w.type == Tag("GRUP")) {
						if (w.size < 24) break;
						if ((int32_t)w.groupType == 1 && IsWorld(w.label)) Children(p + 24, p + w.size, false, 0, 0, 0);
						p += w.size;
					}
					else p += 24 + (uint64_t)w.size;
				}
			}
			pos += h.size;
		}
		return true;
	}
};

}

bool LandHeights::Read(const std::vector<Plugin>& Plugins, const std::string& WorldFile, uint32_t WorldObjectID, uint32_t MaxResolution, Result& Out) {
	Out = Result();
	std::unordered_map<int64_t, LandEntry> lands;
	std::vector<std::string> skipped;
	for (uint32_t i = 0; i < Plugins.size(); i++) {
		Reader file("Data\\" + Plugins[i].Name);
		if (!file.Ok()) { skipped.push_back(Plugins[i].Name); continue; }
		Walker walker{ file, i, Plugins[i].Name, {}, WorldFile, WorldObjectID, lands };
		std::string error;
		if (!walker.Run(error)) skipped.push_back(Plugins[i].Name + " (" + error + ")");
	}
	if (lands.empty()) {
		Out.Message = "no land records for the worldspace in the load order";
		return false;
	}

	int west = INT32_MAX, south = INT32_MAX, east = INT32_MIN, north = INT32_MIN;
	for (const auto& l : lands) {
		west = (std::min)(west, l.second.x); east = (std::max)(east, l.second.x);
		south = (std::min)(south, l.second.y); north = (std::max)(north, l.second.y);
	}
	const uint32_t fullWidth = (uint32_t)(east - west + 1) * 32, fullHeight = (uint32_t)(north - south + 1) * 32;
	const uint32_t longer = (std::max)(fullWidth, fullHeight);
	if (!MaxResolution) MaxResolution = 1024;
	const uint32_t factor = longer > MaxResolution ? (longer + MaxResolution - 1) / MaxResolution : 1;
	Out.Width = (fullWidth + factor - 1) / factor;
	Out.Height = (fullHeight + factor - 1) / factor;
	const float noLand = -1.0e30f;
	Out.Heights.assign((size_t)Out.Width * Out.Height, 0.0f);
	Out.Peaks.assign((size_t)Out.Width * Out.Height, noLand);
	std::vector<uint16_t> counts((size_t)Out.Width * Out.Height, 0);

	std::vector<const LandEntry*> order;
	order.reserve(lands.size());
	for (const auto& l : lands) order.push_back(&l.second);
	std::sort(order.begin(), order.end(), [](const LandEntry* a, const LandEntry* b) { return a->plugin != b->plugin ? a->plugin < b->plugin : a->pos < b->pos; });

	std::vector<uint8_t> raw, data;
	std::unique_ptr<Reader> file;
	uint32_t openPlugin = UINT32_MAX, unreadable = 0;
	for (const LandEntry* l : order) {
		if (l->plugin != openPlugin) { file = std::make_unique<Reader>("Data\\" + Plugins[l->plugin].Name); openPlugin = l->plugin; }
		uint32_t size = 0;
		const uint8_t* vhgt = nullptr;
		if (file->Ok() && RecordData(*file, l->pos, l->header, raw, data, 8192)) vhgt = FindSub(data, Tag("VHGT"), size);
		if (!vhgt && file->Ok() && (l->header.label & RecordCompressed) && RecordData(*file, l->pos, l->header, raw, data, 0))
			vhgt = FindSub(data, Tag("VHGT"), size);
		if (!vhgt || size < 4 + 33 * 33) { unreadable++; continue; }
		float offset;
		memcpy(&offset, vhgt, 4);
		const int8_t* steps = (const int8_t*)(vhgt + 4);
		float rowStart = offset;
		for (int r = 0; r < 32; r++) {
			rowStart += steps[r * 33];
			float v = rowStart;
			const uint32_t globalRow = (uint32_t)(l->y - south) * 32 + r;
			const uint32_t imageRow = (fullHeight - 1 - globalRow) / factor;
			for (int c = 0; c < 32; c++) {
				if (c) v += steps[r * 33 + c];
				const float h = v * 8.0f;
				const size_t k = (size_t)imageRow * Out.Width + ((uint32_t)(l->x - west) * 32 + c) / factor;
				Out.Heights[k] += h;
				Out.Peaks[k] = (std::max)(Out.Peaks[k], h);
				counts[k]++;
			}
		}
		Out.Cells++;
	}
	file.reset();

	float low = FLT_MAX, high = -FLT_MAX;
	for (size_t k = 0; k < counts.size(); k++) {
		if (!counts[k]) continue;
		Out.Heights[k] /= (float)counts[k];
		low = (std::min)(low, Out.Heights[k]);
		high = (std::max)(high, Out.Heights[k]);
	}
	if (!Out.Cells || low > high) {
		Out = Result();
		Out.Message = "the land records hold no heights";
		return false;
	}
	for (size_t k = 0; k < counts.size(); k++) if (!counts[k]) { Out.Heights[k] = low; Out.Peaks[k] = low; }

	Out.Texel = 128.0f * factor;
	Out.West = west * 4096.0f + 0.5f * (factor - 1) * 128.0f;
	Out.North = (north + 1) * 4096.0f - 128.0f - 0.5f * (factor - 1) * 128.0f;
	Out.Low = low;
	Out.High = high;
	Out.CellWest = west; Out.CellSouth = south; Out.CellEast = east; Out.CellNorth = north;
	char text[400];
	sprintf_s(text, "%u cells with land (%d,%d to %d,%d) from %u plugins%s, land heights %.0f to %.0f, %ux%u (%.0f units a texel)%s",
		Out.Cells, west, south, east, north, (unsigned)Plugins.size(), skipped.empty() ? "" : " (some unreadable)", low, high,
		Out.Width, Out.Height, Out.Texel, unreadable ? ", some land records without heights" : "");
	Out.Message = text;
	if (!skipped.empty()) {
		Out.Message += "; not read:";
		for (const std::string& s : skipped) Out.Message += " " + s;
	}
	return true;
}
