#pragma once

// #include "Graphics/IGraphics.h"

#include <unordered_map>
#include <string>

#include "imgui/imgui.h"

// Resource-manager singleton.
class ResourceManager {
public:
    static ResourceManager& GetInstance();
    
    // Disable copying and assignment.
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    
    // Initialize font resources.
    bool initializeFonts(float fontSize);
    
    // Return the font.
    ImFont* getZhFont() const { return m_zhFont; }
    ImFont* getIconFont(int index) const;
    
    // Texture management.
    // BaseTexData* loadTexture(IGraphics* graphics, const char* filepath);
    // void unloadTexture(IGraphics* graphics, BaseTexData* texture);
    // void clearAllTextures(IGraphics* graphics);
    
private:
    ResourceManager() = default;
    ~ResourceManager() = default;
    
    // Find a system font with Chinese glyph coverage.
    std::string findSystemChineseFont();
    
    // Font resources.
    ImFont* m_zhFont = nullptr;
    ImFont* m_iconFonts[3] = {nullptr, nullptr, nullptr};
    
    // Texture resources.
    // std::unordered_map<std::string, BaseTexData*> m_textures;
};
