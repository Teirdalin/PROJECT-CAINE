#include <caine/mod_loader.hpp>
#include <objbase.h>
#include <xmllite.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <sstream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT value) { if (FAILED(value)) throw std::runtime_error("Invalid XML manifest"); }
std::wstring Attribute(IXmlReader* reader, const wchar_t* name, bool required = true) {
    std::wstring result;
    if (reader->MoveToAttributeByName(name, nullptr) == S_OK) {
        const wchar_t* value{}; UINT size{};
        Check(reader->GetValue(&value, &size)); result.assign(value, size);
        Check(reader->MoveToElement());
    }
    if (required && result.empty()) throw std::runtime_error("Manifest attribute is missing");
    return result;
}
std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Invalid manifest text");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::filesystem::path File(const std::filesystem::path& folder, const std::wstring& name, const wchar_t* extension) {
    const std::filesystem::path leaf(name);
    if (name.empty() || name == L"." || name == L".." || name.find_first_of(L"/\\:") != std::wstring::npos ||
        leaf.filename() != leaf || _wcsicmp(leaf.extension().c_str(), extension))
        throw std::runtime_error("Manifest paths must be plain file names with the required extension");
    const auto path = folder / leaf;
    const auto attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Redirected mod file");
    return path;
}
}
namespace caine {
ModInfo ReadModInfo(const std::filesystem::path& folder) {
    ModInfo info{}; info.directory = folder;
    const auto path = File(folder, folder.filename().wstring() + L".xml", L".xml");
    if (std::filesystem::file_size(path) > 65536) throw std::runtime_error("Manifest exceeds 64 KiB");
    ComPtr<IStream> stream;
    Check(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &stream));
    ComPtr<IXmlReader> reader;
    Check(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(reader.GetAddressOf()), nullptr));
    Check(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit));
    Check(reader->SetProperty(XmlReaderProperty_MaxElementDepth, 4));
    Check(reader->SetInput(stream.Get()));
    XmlNodeType type{}; bool root = false, description = false;
    HRESULT next{};
    while ((next = reader->Read(&type)) == S_OK) {
        if (type == XmlNodeType_Element) {
            const wchar_t* raw{}; UINT length{}, depth{};
            Check(reader->GetLocalName(&raw, &length)); Check(reader->GetDepth(&depth));
            const std::wstring name(raw, length);
            if (depth == 0 && !root && name == L"mod") {
                if (Attribute(reader.Get(), L"schemaVersion") != L"1") throw std::runtime_error("Unsupported manifest schema");
                info.id = Utf8(Attribute(reader.Get(), L"id"));
                if (info.id.empty() || info.id.size() > 64 || info.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos)
                    throw std::runtime_error("Invalid stable mod ID");
                info.name = Utf8(Attribute(reader.Get(), L"name"));
                info.version = Utf8(Attribute(reader.Get(), L"version"));
                info.author = Utf8(Attribute(reader.Get(), L"author", false));
                info.binary = File(folder, Attribute(reader.Get(), L"dll"), L".dll");
                info.config = File(folder, Attribute(reader.Get(), L"config"), L".cfg");
                const auto logo = Attribute(reader.Get(), L"logo", false);
                if (!logo.empty()) info.logo = File(folder, logo, L".png");
                std::istringstream version(Utf8(Attribute(reader.Get(), L"minimumFramework")));
                unsigned major{}, minor{}, patch{}; char a{}, b{};
                if (!(version >> major >> a >> minor >> b >> patch) || a != '.' || b != '.' ||
                    major > 255 || minor > 255 || patch > 255 || version.peek() != EOF)
                    throw std::runtime_error("Invalid minimum framework version");
                info.minimumVersion = (major << 16) | (minor << 8) | patch;
                root = true;
            } else if (root && depth == 1 && name == L"description") description = true;
            else throw std::runtime_error("Unexpected XML element");
        } else if ((type == XmlNodeType_Text || type == XmlNodeType_CDATA) && description) {
            const wchar_t* value{}; UINT size{}; Check(reader->GetValue(&value, &size));
            info.description += Utf8(std::wstring(value, size));
        } else if (type == XmlNodeType_EndElement) description = false;
    }
    Check(next);
    if (!root) throw std::runtime_error("Missing mod manifest");
    info.enabled = GetPrivateProfileIntW(L"Mod", L"Enabled", 1, info.config.c_str()) != 0;
    return info;
}
}

