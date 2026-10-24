#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// CD Material Bridge 0.1.1. No executable offsets, game memory hooks, or game-file writes.
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
static HMODULE selfModule;
static volatile LONG initialized;
static std::ofstream logFile;
static void log(const std::string &s)
{
    if (logFile)
    {
        logFile << s << '\n';
        logFile.flush();
    }
}
static void require(bool yes, const char *why)
{
    if (!yes)
        throw std::runtime_error(why);
}
static uint32_t u32(const Bytes &b, size_t p)
{
    require(p <= b.size() && b.size() - p >= 4, "truncated u32");
    uint32_t v;
    memcpy(&v, b.data() + p, 4);
    return v;
}
static uint16_t u16(const Bytes &b, size_t p)
{
    require(p <= b.size() && b.size() - p >= 2, "truncated u16");
    uint16_t v;
    memcpy(&v, b.data() + p, 2);
    return v;
}
static void put32(Bytes &b, size_t p, uint32_t v)
{
    require(p <= b.size() && b.size() - p >= 4, "write bounds");
    memcpy(b.data() + p, &v, 4);
}
static void put16(Bytes &b, size_t p, uint16_t v)
{
    require(p <= b.size() && b.size() - p >= 2, "write bounds");
    memcpy(b.data() + p, &v, 2);
}
static uint32_t rot(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
// Jenkins lookup3, using the game's seed (0x000c5ede).
static uint32_t paHash(const uint8_t *p, size_t n)
{
    uint32_t a = 0xdeba1dcd + uint32_t(n), b = a, c = a;
    auto word = [](const uint8_t *q)
    { uint32_t v; memcpy(&v,q,4); return v; };
    while (n > 12)
    {
        a += word(p);
        b += word(p + 4);
        c += word(p + 8);
        a -= c;
        a ^= rot(c, 4);
        c += b;
        b -= a;
        b ^= rot(a, 6);
        a += c;
        c -= b;
        c ^= rot(b, 8);
        b += a;
        a -= c;
        a ^= rot(c, 16);
        c += b;
        b -= a;
        b ^= rot(a, 19);
        a += c;
        c -= b;
        c ^= rot(b, 4);
        b += a;
        p += 12;
        n -= 12;
    }
    if (!n)
        return c;
    uint8_t tail[12] = {};
    memcpy(tail, p, n);
    a += word(tail);
    b += word(tail + 4);
    c += word(tail + 8);
    c = (c ^ b) - rot(b, 14);
    a = (a ^ c) - rot(c, 11);
    b = (b ^ a) - rot(a, 25);
    c = (c ^ b) - rot(b, 16);
    a = (a ^ c) - rot(c, 4);
    b = (b ^ a) - rot(a, 14);
    c = (c ^ b) - rot(b, 24);
    return c;
}
static uint32_t paHash(const std::string &s) { return paHash((const uint8_t *)s.data(), s.size()); }
static std::string lower(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    return s;
}
static std::string trim(std::string s)
{
    auto a = s.find_first_not_of(" \t\r\n");
    return a == s.npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
static std::string normalized(std::string s)
{
    std::replace(s.begin(), s.end(), '\\', '/');
    s = lower(s);
    while (!s.empty() && s.front() == '/')
        s.erase(0, 1);
    while (!s.empty() && s.back() == '/')
        s.pop_back();
    require(!s.empty() && s.find(':') == s.npos && s.find("..") == s.npos, "invalid relative path");
    return s;
}
static Bytes readFile(const fs::path &p, size_t limit = 128 * 1024 * 1024)
{
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    require(bool(f), "file open failed");
    auto n = f.tellg();
    require(n >= 0 && uint64_t(n) <= limit, "file size out of range");
    Bytes b(size_t(n), 0);
    f.seekg(0);
    if (n)
    {
        f.read((char *)b.data(), n);
        require(bool(f), "short file read");
    }
    return b;
}
static std::string sha(const uint8_t *data, size_t n)
{
    require(n <= MAXDWORD, "hash too large");
    uint8_t d[32];
    auto status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(data), ULONG(n), d, 32);
    require(status >= 0, "SHA256 failed");
    static const char h[] = "0123456789abcdef";
    std::string out;
    for (auto v : d)
    {
        out += h[v >> 4];
        out += h[v & 15];
    }
    return out;
}
static std::string sha(const Bytes &b) { return sha(b.data(), b.size()); }
static uint32_t number(const std::string &s)
{
    size_t n = 0;
    auto v = std::stoull(s, &n, 0);
    require(n == s.size() && v <= UINT32_MAX, "bad integer");
    return uint32_t(v);
}
struct Rule
{
    std::string name, path, digest, fourcc;
    uint32_t width, height, mips, base, checksum;
    bool found = false, valid = false;
};
static std::vector<Rule> loadRules(const fs::path &dir)
{
    std::vector<fs::path> files;
    std::vector<Rule> rules;
    if (!fs::exists(dir))
        return rules;
    for (auto &e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == L".ini")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());
    require(files.size() <= 256, "too many rule files");
    for (auto &f : files)
    {
        try
        {
            auto bytes = readFile(f, 256 * 1024);
            std::string s(bytes.begin(), bytes.end());
            if (s.compare(0, 3, "\xef\xbb\xbf") == 0)
                s.erase(0, 3);
            std::istringstream stream(s);
            std::string line, section;
            std::map<std::string, std::string> values;
            std::vector<Rule> fileRules;
            auto finish = [&]()
            {
                if (section.empty())
                    return;
                require(section.rfind("texture:", 0) == 0, "unknown rule section");
                if (values.count("enabled") && values.at("enabled") == "0")
                {
                    values.clear();
                    return;
                }
                const std::set<std::string> keys = {"enabled", "path", "width", "height", "mips", "fourcc", "baseflags", "pixelsha256"};
                for (auto &v : values)
                    require(keys.count(v.first) != 0, "unknown rule key");
                Rule r;
                r.name = f.filename().u8string() + "/" + section;
                r.path = normalized(values.at("path"));
                require(r.path.size() < 1024 && r.path.rfind("character/texture/", 0) == 0, "only character/texture paths supported");
                require(r.path.size() > 4 && r.path.substr(r.path.size() - 4) == ".dds", "DDS path required");
                r.width = number(values.at("width"));
                r.height = number(values.at("height"));
                r.mips = number(values.at("mips"));
                r.base = number(values.at("baseflags"));
                require(r.width && r.height && r.width <= 16384 && r.height <= 16384 && r.mips && r.mips <= 15, "bad dimensions/mips");
                require(!(r.base & 1), "BaseFlags must have alpha bit cleared");
                r.fourcc = values.at("fourcc");
                require(r.fourcc == "DXT3" || r.fourcc == "DXT5", "v0.1 supports BC2/BC3 cutout only");
                r.digest = lower(values.at("pixelsha256"));
                require(r.digest.size() == 64 && r.digest.find_first_not_of("0123456789abcdef") == r.digest.npos, "bad pixel SHA256");
                r.checksum = paHash("/" + r.path);
                fileRules.push_back(r);
                values.clear();
            };
            while (std::getline(stream, line))
            {
                line = trim(line);
                if (line.empty() || line[0] == ';' || line[0] == '#')
                    continue;
                if (line[0] == '[')
                {
                    finish();
                    require(line.back() == ']', "bad section");
                    section = lower(line.substr(1, line.size() - 2));
                }
                else
                {
                    auto i = line.find('=');
                    require(!section.empty() && i != line.npos, "bad rule line");
                    auto key = lower(trim(line.substr(0, i)));
                    require(!values.count(key), "duplicate key");
                    values[key] = trim(line.substr(i + 1));
                }
            }
            finish();
            rules.insert(rules.end(), fileRules.begin(), fileRules.end());
        }
        catch (const std::exception &e)
        {
            log("RULE_FILE_SKIPPED " + f.filename().u8string() + ": " + e.what());
        }
    }
    require(rules.size() <= 1024, "too many texture rules");
    return rules;
}
struct Group
{
    std::string name;
    uint32_t checksum, flags;
};
static std::vector<Group> groups(const Bytes &b)
{
    require(b.size() >= 16 && paHash(b.data() + 12, b.size() - 12) == u32(b, 4), "invalid PAPGT checksum");
    size_t n = u32(b, 8) & 255, at = 12 + 12 * n;
    uint32_t len = u32(b, at);
    require(at + 4 + len == b.size(), "invalid PAPGT size");
    std::vector<Group> out;
    std::set<std::string> seen;
    for (size_t i = 0; i < n; i++)
    {
        size_t p = at + 4 + u32(b, 12 + i * 12 + 4);
        require(p >= at + 4 && p < b.size(), "PAPGT name bounds");
        std::string name;
        while (p < b.size() && b[p])
            name += char(b[p++]);
        require(p < b.size(), "PAPGT unterminated name");
        name = normalized(name);
        require(name.find('/') == name.npos && !seen.count(name), "PAPGT duplicate/invalid group");
        seen.insert(name);
        out.push_back({name, u32(b, 12 + i * 12 + 8), u32(b, 12 + i * 12)});
    }
    return out;
}
static std::string linkedName(const Bytes &b, size_t start, size_t length, uint32_t offset, bool allowEmptyRoot = false)
{
    std::string out;
    std::set<uint32_t> visited;
    while (offset != UINT32_MAX)
    {
        require(visited.size() < 256 && visited.insert(offset).second && uint64_t(offset) + 5 <= length, "bad name chain");
        size_t p = start + offset;
        size_t n = b[p + 4];
        require(uint64_t(offset) + 5 + n <= length, "bad name fragment");
        out = std::string((const char *)b.data() + p + 5, n) + out;
        offset = u32(b, p);
    }
    if (allowEmptyRoot && out.empty())
        return "";
    return normalized(out);
}
static bool shapeMatches(const Bytes &d, const Rule &r)
{
    return d.size() >= 128 && !memcmp(d.data(), "DDS ", 4) && u32(d, 4) == 124 && u32(d, 12) == r.height && u32(d, 16) == r.width && u32(d, 28) == r.mips && !memcmp(d.data() + 84, r.fourcc.data(), 4);
}
static void verifyPayloads(const fs::path &root, const Bytes &papgt, std::vector<Rule> &rules)
{
    for (auto &g : groups(papgt))
    {
        if (std::all_of(rules.begin(), rules.end(), [](const Rule &r)
                        { return r.found; }))
            break;
        auto index = root / g.name / "0.pamt";
        if (!fs::exists(index) && (g.flags & 1))
            continue;
        auto b = readFile(index);
        require(b.size() >= 12 && u32(b, 0) == g.checksum, "PAMT mount checksum changed");
        require(paHash(b.data() + 12, b.size() - 12) == g.checksum, "PAMT body checksum invalid");
        size_t pazCount = u32(b, 4), pos = 12 + 12 * pazCount;
        require(pazCount <= 65535 && pos <= b.size(), "bad PAZ count");
        size_t dl = u32(b, pos), ds = pos + 4;
        pos = ds + dl;
        require(pos <= b.size(), "directory block truncated");
        size_t nl = u32(b, pos), ns = pos + 4;
        pos = ns + nl;
        require(pos <= b.size(), "name block truncated");
        size_t fc = u32(b, pos), folders = pos + 4;
        pos = folders + fc * 16;
        require(pos <= b.size(), "folder table truncated");
        size_t count = u32(b, pos), entries = pos + 4;
        require(count < 10000000 && entries + count * 20 == b.size(), "file table length mismatch");
        for (size_t i = 0; i < fc; i++)
        {
            auto f = folders + i * 16;
            auto folder = linkedName(b, ds, dl, u32(b, f + 4), true);
            std::vector<size_t> wanted;
            for (size_t r = 0; r < rules.size(); r++)
                if (!rules[r].found && rules[r].path.substr(0, rules[r].path.rfind('/')) == folder)
                    wanted.push_back(r);
            if (wanted.empty())
                continue;
            size_t first = u32(b, f + 8), num = u32(b, f + 12);
            require(first <= count && num <= count - first, "folder file bounds");
            for (size_t j = first; j < first + num; j++)
            {
                size_t at = entries + j * 20;
                std::string path = folder + "/" + linkedName(b, ns, nl, u32(b, at));
                for (auto ri : wanted)
                {
                    auto &r = rules[ri];
                    if (r.found || r.path != path)
                        continue;
                    r.found = true;
                    try
                    {
                        uint32_t offset = u32(b, at + 4), stored = u32(b, at + 8), size = u32(b, at + 12);
                        uint16_t paz = u16(b, at + 16), flags = u16(b, at + 18);
                        require(paz < pazCount && (flags == 0 || flags == 1) && stored == size && size >= 128 && size <= 128 * 1024 * 1024, "DDS archive encoding unsupported");
                        std::ifstream file(root / g.name / (std::to_string(paz) + ".paz"), std::ios::binary | std::ios::ate);
                        require(bool(file), "PAZ open failed");
                        auto total = file.tellg();
                        require(total >= 0 && uint64_t(offset) + stored <= uint64_t(total), "PAZ payload bounds");
                        Bytes d(size, 0);
                        file.seekg(offset);
                        file.read((char *)d.data(), size);
                        require(bool(file), "PAZ short read");
                        require(shapeMatches(d, r), "DDS shape mismatch");
                        require(sha(d.data() + 128, d.size() - 128) == r.digest, "mounted pixels differ (mod off, overwritten or resized)");
                        r.valid = true;
                        log("ASSET_VERIFIED " + r.path + " in " + g.name);
                    }
                    catch (const std::exception &e)
                    {
                        log("ASSET_SKIPPED " + r.path + ": " + e.what());
                    }
                }
            }
        }
    }
    for (auto &r : rules)
        if (!r.found)
            log("ASSET_MISSING " + r.path);
}
struct Table
{
    Bytes prefix, names;
    std::vector<Bytes> headers, entries, collisions;
    std::vector<uint32_t> keys;
    explicit Table(const Bytes &b)
    {
        require(b.size() >= 28 && u32(b, 8) == 148, "unsupported PATHC format");
        uint64_t hc = u32(b, 12), ec = u32(b, 16), cc = u32(b, 20), nc = u32(b, 24);
        require(hc && hc < 65535 && ec <= 10000000 && cc <= 256 && 28 + 148 * hc + 24 * ec + 24 * cc + nc == b.size(), "invalid PATHC counts");
        prefix.assign(b.begin(), b.begin() + 28);
        size_t p = 28;
        for (size_t i = 0; i < hc; i++, p += 148)
            headers.emplace_back(b.begin() + p, b.begin() + p + 148);
        for (size_t i = 0; i < ec; i++, p += 4)
        {
            auto k = u32(b, p);
            require(keys.empty() || k > keys.back(), "PATHC keys unsorted/duplicate");
            keys.push_back(k);
        }
        for (size_t i = 0; i < ec; i++, p += 20)
            entries.emplace_back(b.begin() + p, b.begin() + p + 20);
        for (size_t i = 0; i < cc; i++, p += 24)
            collisions.emplace_back(b.begin() + p, b.begin() + p + 24);
        names.assign(b.begin() + p, b.end());
        // Preserve unrelated collision rows, including existing out-of-range endpoints.
        // Only require a valid collision range when a requested path uses that row.
        for (auto &e : entries)
            require(u16(e, 0) < hc || u16(e, 0) == 65535, "PATHC entry bounds");
        for (auto &c : collisions)
            require(u16(c, 4) < hc && u32(c, 0) < names.size(), "PATHC collision bounds");
    }
    Bytes *reference(const Rule &r, size_t &headerField)
    {
        auto i = std::lower_bound(keys.begin(), keys.end(), r.checksum);
        if (i == keys.end() || *i != r.checksum)
            return nullptr;
        auto &e = entries[i - keys.begin()];
        headerField = 0;
        if (u16(e, 0) != 65535)
            return &e;
        require(e[2] <= e[3] && e[3] < collisions.size(), "requested collision range invalid");
        Bytes *found = nullptr;
        for (size_t j = e[2]; j <= e[3]; j++)
        {
            auto &c = collisions[j];
            size_t p = u32(c, 0);
            std::string name;
            while (p < names.size() && names[p])
                name += char(names[p++]);
            require(p < names.size(), "PATHC collision name unterminated");
            if (normalized(name) == r.path)
            {
                require(!found, "ambiguous PATHC collision");
                found = &c;
            }
        }
        headerField = 4;
        return found;
    }
    bool apply(const Rule &r)
    {
        size_t field;
        Bytes *ref = reference(r, field);
        require(ref, "texture not registered in PATHC");
        uint16_t hi = u16(*ref, field);
        auto h = headers[hi];
        require(shapeMatches(h, r), "PATHC DDS shape mismatch");
        uint32_t flags = u32(h, 124);
        require((flags & ~1u) == r.base, "PATHC base flags changed");
        if (flags & 1)
        {
            log("ALREADY_ENABLED " + r.path);
            return false;
        }
        size_t users = 0;
        for (auto &e : entries)
            if (u16(e, 0) == hi)
                users++;
        for (auto &c : collisions)
            if (u16(c, 4) == hi)
                users++;
        put32(h, 124, flags | 1);
        if (users == 1)
            headers[hi] = h;
        else
        {
            require(headers.size() < 65535, "PATHC header index exhausted");
            put16(*ref, field, uint16_t(headers.size()));
            headers.push_back(h);
        }
        log("ALPHA_ENABLED " + r.path + (users == 1 ? " (private header)" : " (isolated shared header)"));
        return true;
    }
    Bytes encode() const
    {
        Bytes b = prefix;
        put32(b, 12, uint32_t(headers.size()));
        for (auto &h : headers)
            b.insert(b.end(), h.begin(), h.end());
        for (auto k : keys)
        {
            size_t at = b.size();
            b.resize(at + 4);
            put32(b, at, k);
        }
        for (auto &e : entries)
            b.insert(b.end(), e.begin(), e.end());
        for (auto &c : collisions)
            b.insert(b.end(), c.begin(), c.end());
        b.insert(b.end(), names.begin(), names.end());
        return b;
    }
};
static fs::path modulePath(HMODULE h)
{
    std::wstring s(32768, 0);
    DWORD n = GetModuleFileNameW(h, s.data(), DWORD(s.size()));
    require(n && n < s.size(), "module path unavailable");
    s.resize(n);
    return fs::path(s);
}
using AddPath = bool(WINAPI *)(const wchar_t *, const wchar_t *, int);
using RemovePath = void(WINAPI *)(const wchar_t *);
static RemovePath removeRedirect;
static AddPath loaderAPI()
{
    for (const wchar_t *name : {L"winmm.dll", L"dinput8.dll", L"version.dll", L"dsound.dll", L"dxgi.dll", L"winhttp.dll", L"d3d11.dll", L"d3d12.dll", L"xinput1_3.dll", L"xinput1_4.dll"})
    {
        auto h = GetModuleHandleW(name);
        if (h && GetProcAddress(h, "IsUltimateASILoader"))
        {
            auto api = reinterpret_cast<AddPath>(GetProcAddress(h, "AddVirtualPathForOverloadW"));
            removeRedirect = reinterpret_cast<RemovePath>(GetProcAddress(h, "RemoveVirtualPathFromOverloadW"));
            if (api && removeRedirect)
                return api;
        }
    }
    return nullptr;
}
static void run()
{
    auto exe = modulePath(nullptr);
    if (_wcsicmp(exe.filename().c_str(), L"CrimsonDesert.exe"))
        return;
    auto home = modulePath(selfModule).parent_path() / "CDMaterialBridge";
    fs::create_directories(home);
    logFile.open(home / "CDMaterialBridge.log", std::ios::trunc);
    log("CD Material Bridge 0.1.1; runtime-only registry redirect");
    auto api = loaderAPI();
    require(api, "Ultimate ASI Loader with AddVirtualPathForOverloadW required");
    auto rules = loadRules(home / "rules");
    log("RULES " + std::to_string(rules.size()));
    if (rules.empty())
        return;
    auto root = exe.parent_path().parent_path();
    auto registry = root / "meta/0.pathc";
    auto raw = readFile(registry), papgt = readFile(root / "meta/0.papgt", 1024 * 1024);
    verifyPayloads(root, papgt, rules);
    Table table(raw);
    size_t changed = 0;
    for (auto &r : rules)
        if (r.valid)
        {
            try
            {
                if (table.apply(r))
                    changed++;
            }
            catch (const std::exception &e)
            {
                log("REGISTRY_SKIPPED " + r.path + ": " + e.what());
            }
        }
    if (!changed)
    {
        log("NO_REDIRECT: no correction needed");
        return;
    }
    auto candidate = table.encode();
    Table validate(candidate);
    // A concurrent Mount must never be hidden behind a stale registry copy.
    require(readFile(registry) == raw && readFile(root / "meta/0.papgt", 1024 * 1024) == papgt, "mount changed during initialization; restart after Mount completes");
    auto cache = home / "cache";
    fs::create_directories(cache);
    auto dest = cache / (sha(candidate) + ".pathc");
    // Content-addressed cache avoids conflicts between simultaneous processes; never restore to meta.
    if (!fs::exists(dest) || readFile(dest) != candidate)
    {
        auto tmp = cache / (std::to_string(GetCurrentProcessId()) + ".tmp");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            require(bool(out), "cache create failed");
            out.write((char *)candidate.data(), candidate.size());
            out.flush();
            require(bool(out), "cache write failed");
        }
        require(readFile(tmp) == candidate, "cache verification failed");
        require(MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "cache commit failed");
    }
    require(api(registry.c_str(), dest.c_str(), 0), "loader refused registry redirect (possible another registry override)");
    try
    {
        require(readFile(registry) == candidate, "loader redirect readback failed");
    }
    catch (...)
    {
        removeRedirect(registry.c_str());
        throw;
    }
    log("REDIRECT_READY corrections=" + std::to_string(changed) + " source_sha256=" + sha(raw) + " runtime_sha256=" + sha(candidate));
    log("Original game files are unchanged. Disable ASI and restart to remove this redirect.");
}
extern "C" __declspec(dllexport) void InitializeASI()
{
    if (InterlockedCompareExchange(&initialized, 1, 0))
        return;
    try
    {
        run();
    }
    catch (const std::exception &e)
    {
        log(std::string("INACTIVE: ") + e.what());
    }
    catch (...)
    {
        log("INACTIVE: unknown error");
    }
}
BOOL WINAPI DllMain(HMODULE h, DWORD why, LPVOID)
{
    if (why == DLL_PROCESS_ATTACH)
    {
        selfModule = h;
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}
