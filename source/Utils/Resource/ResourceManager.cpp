#include "ResourceManager.h"

#include <filesystem>
#include <vector>
#include <algorithm>

// #include "Fonts/Consolas.h"
// #include "Fonts/MiSansVF.h"
// #include "Fonts/SourceHanSansHWSC_VF.h"
// #include "Fonts/LXGWWenKaiMonoRegular.h"
// #include "Fonts/fontawesome-brands.h"
// #include "Fonts/fontawesome-regular.h"
// #include "Fonts/fontawesome-solid.h"
// #include "Fonts/fontawesome.h"
#include "Utils/Logger.h"

ResourceManager& ResourceManager::GetInstance() {
    static ResourceManager instance;
    return instance;
}

std::string ResourceManager::findSystemChineseFont() {
    const std::vector<std::string> fontDirs = {
        "/system/fonts",
        "/system/font",
        "/data/fonts"
    };

    const std::vector<std::string> chineseFonts = {
        "MiSansVF.ttf",
        "SysSans-Hans-Regular.ttf",
        "ZUKChinese.ttf"
    };

    // Enumerate the font directory.
    for (const auto& dir : fontDirs) {
        if (!std::filesystem::exists(dir)) {
            continue;
        }

        // Search preferred fonts in order.
        for (const auto& fontName : chineseFonts) {
            std::string fontPath = dir + "/" + fontName;
            if (std::filesystem::exists(fontPath)) {
                FLOGI("Found Chinese font: {}", fontPath);
                return fontPath;
            }
        }

        // If no preferred font is found, sort matching fonts by file size.
        try {
            std::vector<std::pair<std::uintmax_t, std::string>> candidateFonts;
            
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    std::string extension = entry.path().extension().string();
                    
                    // Compare lowercase names.
                    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                    std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
                    
                    // Match names associated with Chinese glyph coverage.
                    if ((extension == ".ttf" || extension == ".otf") &&
                        (filename.find("cjk") != std::string::npos ||
                         filename.find("hans") != std::string::npos ||
                         filename.find("chinese") != std::string::npos ||
                         filename.find("sc") != std::string::npos ||
                         filename.find("noto") != std::string::npos)) {
                        std::uintmax_t fileSize = std::filesystem::file_size(entry.path());
                        candidateFonts.emplace_back(fileSize, entry.path().string());
                    }
                }
            }
            
            // Sort by descending file size.
            if (!candidateFonts.empty()) {
                std::sort(candidateFonts.begin(), candidateFonts.end(), 
                    [](const auto& a, const auto& b) { return a.first > b.first; });
                
                const auto& selectedFont = candidateFonts.front();
                FLOGI("Found Chinese font (fallback, size: {} bytes): {}", 
                     selectedFont.first, selectedFont.second);
                return selectedFont.second;
            }
        } catch (const std::exception& e) {
            FLOGW("Error scanning directory {}: {}", dir, e.what());
        }
    }

    FLOGW("No Chinese font found in system directories");
    return "";
}

bool ResourceManager::initializeFonts(float fontSize) {
    ImGuiIO& io = ImGui::GetIO();

    // Try system fonts with Chinese glyph coverage.
    std::string systemFontPath = findSystemChineseFont();
    
    if (!systemFontPath.empty()) {
        // Use a system font.
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = true;
        // Font weight scale: below 1.0 is thinner, above 1.0 is thicker.
        fontConfig.RasterizerMultiply = 1.1f;

        m_zhFont = io.Fonts->AddFontFromFileTTF(
            systemFontPath.c_str(),
            fontSize,
            &fontConfig,
            io.Fonts->GetGlyphRangesChineseFull());
        
        if (m_zhFont) {
            FLOGI("Successfully loaded system Chinese font: {}", systemFontPath);
        } else {
            FLOGE("Failed to load system font from: {}", systemFontPath);
        }
    }
    
    // Use the embedded font if system-font loading fails.
    if (!m_zhFont) {
        FLOGW("Falling back to embedded font");
        // m_zhFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(Consolas_compressed_data_base85, 20.0f);
        // m_zhFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(SourceHanSansHWSC_VF_compressed_data_base85, 30.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
        // m_zhFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(MiSansVF_compressed_data_base85, 27.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
        // m_zhFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(LXGWWenKaiMonoRegular_compressed_data_base85, 25.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    }
    
    IM_ASSERT(m_zhFont != nullptr);

    // Load the icon font.
    // static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
    // ImFontConfig icons_config;
    // icons_config.MergeMode = true;
    // icons_config.PixelSnapH = true;
    // icons_config.OversampleH = 3.0f;
    // icons_config.OversampleV = 3.0f;
    // icons_config.SizePixels = fontSize;

    // m_iconFonts[0] = io.Fonts->AddFontFromMemoryCompressedTTF(
    //     (const void*)&font_awesome_brands_compressed_data, 
    //     sizeof(font_awesome_brands_compressed_data), 
    //     0.0f, &icons_config, icons_ranges
    // );

    // m_iconFonts[1] = io.Fonts->AddFontFromMemoryCompressedTTF(
    //     (const void*)&font_awesome_regular_compressed_data, 
    //     sizeof(font_awesome_regular_compressed_data), 
    //     0.0f, &icons_config, icons_ranges
    // );

    // m_iconFonts[2] = io.Fonts->AddFontFromMemoryCompressedTTF(
    //     (const void*)&font_awesome_solid_compressed_data, 
    //     sizeof(font_awesome_solid_compressed_data), 
    //     0.0f, &icons_config, icons_ranges
    // );

    return m_zhFont != nullptr;
}

ImFont* ResourceManager::getIconFont(int index) const {
    if (index >= 0 && index < 3) {
        return m_iconFonts[index];
    }
    return nullptr;
}

// BaseTexData* ResourceManager::loadTexture(IGraphics* graphics, const char* filepath) {
//     if (!graphics) return nullptr;
    
//     std::string key(filepath);
//     auto it = m_textures.find(key);
//     if (it != m_textures.end()) {
//         return it->second;
//     }
    
//     BaseTexData* texture = graphics->LoadTextureFromFile(filepath);
//     if (texture) {
//         m_textures[key] = texture;
//     }
//     return texture;
// }

// void ResourceManager::unloadTexture(IGraphics* graphics, BaseTexData* texture) {
//     if (!graphics || !texture) return;
    
//     for (auto it = m_textures.begin(); it != m_textures.end(); ++it) {
//         if (it->second == texture) {
//             graphics->DeleteTexture(texture);
//             m_textures.erase(it);
//             break;
//         }
//     }
// }

// void ResourceManager::clearAllTextures(IGraphics* graphics) {
//     if (!graphics) return;
    
//     for (auto& pair : m_textures) {
//         graphics->DeleteTexture(pair.second);
//     }
//     m_textures.clear();
// }
