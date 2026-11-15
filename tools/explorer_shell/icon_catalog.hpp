#pragma once

#include <cstddef>
#include <cstdint>

namespace micant::explorer {

enum class IconId : uint8_t {
    Folder, FolderOpen, Documents, Downloads, Pictures, Music, Videos, Desktop,
    Computer, HardDisk, RemovableDrive, TrashEmpty, TrashFull, Network,
    GenericText, Pdf, Archive, Image, Audio, Video, Executable, SharedLibrary,
    Driver, Json, Xml, Count,
};

struct IconAsset {
    IconId id;
    const char16_t* category;
    const char16_t* fileName;
};

inline constexpr IconAsset IconAssets[] = {
    {IconId::Folder, u"places", u"folder.png"},
    {IconId::FolderOpen, u"places", u"folder-open.png"},
    {IconId::Documents, u"places", u"folder-documents.png"},
    {IconId::Downloads, u"places", u"folder-download.png"},
    {IconId::Pictures, u"places", u"folder-images.png"},
    {IconId::Music, u"places", u"folder-music.png"},
    {IconId::Videos, u"places", u"folder-videos.png"},
    {IconId::Desktop, u"places", u"user-desktop.png"},
    {IconId::Computer, u"devices", u"computer.png"},
    {IconId::HardDisk, u"devices", u"drive-harddisk.png"},
    {IconId::RemovableDrive, u"devices", u"drive-removable-media.png"},
    {IconId::TrashEmpty, u"places", u"user-trash.png"},
    {IconId::TrashFull, u"places", u"user-trash-full.png"},
    {IconId::Network, u"apps", u"network-workgroup.png"},
    {IconId::GenericText, u"mimes", u"text-x-generic.png"},
    {IconId::Pdf, u"mimes", u"application-pdf.png"},
    {IconId::Archive, u"mimes", u"package-x-generic.png"},
    {IconId::Image, u"mimes", u"image-x-generic.png"},
    {IconId::Audio, u"mimes", u"audio-x-generic.png"},
    {IconId::Video, u"mimes", u"video-x-generic.png"},
    {IconId::Executable, u"mimes", u"application-x-executable.png"},
    {IconId::SharedLibrary, u"mimes", u"application-x-sharedlib.png"},
    {IconId::Driver, u"mimes", u"application-x-object.png"},
    {IconId::Json, u"mimes", u"application-json.png"},
    {IconId::Xml, u"mimes", u"text-xml.png"},
};

inline constexpr const IconAsset* FindIconAsset(IconId id) noexcept {
    for (const auto& asset : IconAssets) if (asset.id == id) return &asset;
    return nullptr;
}

inline constexpr char asciiLower(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

inline IconId ResolveFileIcon(const char* path) noexcept {
    if (!path) return IconId::GenericText;
    const char* extension = nullptr;
    for (const char* cursor = path; *cursor; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') extension = nullptr;
        else if (*cursor == '.') extension = cursor + 1;
    }
    if (!extension || !*extension) return IconId::GenericText;
    auto equals = [&](const char* expected) noexcept {
        size_t index = 0;
        while (extension[index] && expected[index] &&
               asciiLower(extension[index]) == expected[index]) ++index;
        return !extension[index] && !expected[index];
    };
    if (equals("pdf")) return IconId::Pdf;
    if (equals("zip") || equals("7z")) return IconId::Archive;
    if (equals("png") || equals("jpg") || equals("jpeg") || equals("svg"))
        return IconId::Image;
    if (equals("mp3") || equals("wav")) return IconId::Audio;
    if (equals("mp4")) return IconId::Video;
    if (equals("exe")) return IconId::Executable;
    if (equals("dll")) return IconId::SharedLibrary;
    if (equals("sys")) return IconId::Driver;
    if (equals("json")) return IconId::Json;
    if (equals("xml")) return IconId::Xml;
    if (equals("txt") || equals("ini")) return IconId::GenericText;
    return IconId::GenericText;
}

} // namespace micant::explorer
