#pragma once
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <share.h>
#include <string>
#include <vector>

namespace LogFiles {

	inline std::string FileName(const char* base, const SYSTEMTIME& t, unsigned n = 1) {
		char name[160];
		if (n < 2)
			snprintf(name, sizeof(name), "%s_%04u-%02u-%02u_%02u-%02u-%02u.log", base, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
		else
			snprintf(name, sizeof(name), "%s_%04u-%02u-%02u_%02u-%02u-%02u-%u.log", base, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, n);
		return name;
	}

	inline bool IsLogName(const std::string& name, const char* base) {
		const size_t baseLength = strlen(base);
		static const char stamp[] = "_0000-00-00_00-00-00";
		const size_t stampLength = sizeof(stamp) - 1;
		if (name.size() < baseLength + stampLength + 4) return false;
		for (size_t i = 0; i < baseLength; i++)
			if (tolower((unsigned char)name[i]) != tolower((unsigned char)base[i])) return false;
		for (size_t i = 0; i < stampLength; i++) {
			const char c = name[baseLength + i];
			if (stamp[i] == '0' ? !isdigit((unsigned char)c) : c != stamp[i]) return false;
		}
		size_t pos = baseLength + stampLength;
		if (name[pos] == '-') {
			const size_t digits = ++pos;
			while (pos < name.size() && isdigit((unsigned char)name[pos])) pos++;
			if (pos == digits) return false;
		}
		return name.size() - pos == 4 && _stricmp(name.c_str() + pos, ".log") == 0;
	}

	inline std::vector<std::string> OldestToRemove(std::vector<std::string> names, size_t keep, size_t baseLength) {
		std::vector<std::string> remove;
		if (keep == 0 || names.size() < keep) return remove;
		std::sort(names.begin(), names.end(), [baseLength](const std::string& a, const std::string& b) {
			return _stricmp(a.c_str() + baseLength, b.c_str() + baseLength) < 0;
		});
		remove.assign(names.begin(), names.begin() + (names.size() - (keep - 1)));
		return remove;
	}

	inline int LinePrefix(char* out, size_t size, const SYSTEMTIME& t) {
		return snprintf(out, size, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
	}

	inline char* FormatLine(char* stack, size_t stackSize, std::string& heap, const SYSTEMTIME& t, const char* message, va_list args,
		size_t& length, size_t& prefix) {
		prefix = (size_t)LinePrefix(stack, stackSize, t);
		const size_t room = stackSize - prefix;
		va_list copy;
		va_copy(copy, args);
		int written = vsnprintf(stack + prefix, room, message, copy);
		va_end(copy);
		const size_t messageLength = written > 0 ? (size_t)written : 0;
		char* line = stack;
		if (messageLength >= room) {
			heap.assign(stack, prefix);
			heap.resize(prefix + messageLength + 1);
			vsnprintf(&heap[prefix], messageLength + 1, message, args);
			line = &heap[0];
		}
		line[prefix + messageLength] = '\n';
		length = prefix + messageLength + 1;
		return line;
	}

	inline bool Exists(const std::string& path) {
		return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
	}

	inline std::string NewLogPath(const std::string& folder, const char* base, const SYSTEMTIME& now) {
		std::string path = folder + "\\" + FileName(base, now);
		for (unsigned n = 2; Exists(path) && n < 100; n++) path = folder + "\\" + FileName(base, now, n);
		return path;
	}

	inline bool AdoptOldLog(const std::string& oldPath, const std::string& folder, const char* base) {
		WIN32_FILE_ATTRIBUTE_DATA data;
		if (!GetFileAttributesExA(oldPath.c_str(), GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
		SYSTEMTIME utc, local;
		if (!FileTimeToSystemTime(&data.ftLastWriteTime, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return false;
		return MoveFileExA(oldPath.c_str(), NewLogPath(folder, base, local).c_str(), MOVEFILE_COPY_ALLOWED) != 0;
	}

	inline unsigned PruneOldLogs(const std::string& folder, const char* base, size_t keep) {
		std::vector<std::string> names;
		WIN32_FIND_DATAA found;
		HANDLE search = FindFirstFileA((folder + "\\" + base + "_*.log").c_str(), &found);
		if (search != INVALID_HANDLE_VALUE) {
			do {
				if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && IsLogName(found.cFileName, base)) names.push_back(found.cFileName);
			} while (FindNextFileA(search, &found));
			FindClose(search);
		}
		unsigned deleted = 0;
		for (const std::string& name : OldestToRemove(names, keep, strlen(base)))
			if (DeleteFileA((folder + "\\" + name).c_str())) deleted++;
		return deleted;
	}

	struct NewLog {
		FILE* file = nullptr;
		std::string name;
		bool adopted = false;
		unsigned deleted = 0;
	};

	inline NewLog OpenNewLog(const std::vector<std::string>& gameFolders, const char* folderName, const char* base, size_t keep, const SYSTEMTIME& now) {
		NewLog log;
		for (const std::string& gameFolder : gameFolders) {
			const std::string folder = gameFolder + folderName;
			if (!CreateDirectoryA(folder.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) continue;
			log.adopted = AdoptOldLog(gameFolder + base + ".log", folder, base);
			log.deleted = PruneOldLogs(folder, base, keep);
			const std::string path = NewLogPath(folder, base, now);
			log.file = _fsopen(path.c_str(), "w", _SH_DENYWR);
			if (!log.file) continue;
			log.name = std::string(folderName) + "\\" + path.substr(folder.size() + 1);
			return log;
		}
		return NewLog();
	}

}
