#pragma once

#include "IGraphicsBackend.h"

#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>

#include <vector>

/**
 * Vulkan implementation of IGraphicsBackend for the ImGui overlay. Init precedes
 * BeginFrame/EndFrame and Shutdown releases resources. Two frames in flight use fences and
 * semaphores; presentation-completion reuse remains a known limitation.
 *
 *
 *
 *
 *
 *
 *
 */
class VulkanBackend : public IGraphicsBackend
{
public:
    /// Initialize instance, surface, swapchain and ImGui renderer.
    bool Init(ANativeWindow *window, int width, int height) override;
    /// Wait for the frame fence, acquire an image and start an ImGui frame.
    void BeginFrame() override;
    /// Record commands, submit rendering and present.
    void EndFrame() override;
    /// Release Vulkan resources and shut down the ImGui renderer.
    void Shutdown() override;
    /// Whether backend initialization completed.
    bool IsReady() const override;

private:
    /// Maximum frames in flight.
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    // Vulkan initialization stages.
    bool CreateInstance();                       ///< Create the instance with Android surface support.
    bool CreateSurface(ANativeWindow *window);   ///< Create VkSurfaceKHR from ANativeWindow.
    bool SelectPhysicalDevice();                 ///< Select a physical device with graphics and presentation support.
    bool CreateLogicalDevice();                  ///< Create the device and obtain its graphics queue.
    bool CreateSwapchain(int width, int height); ///< Create the swapchain with the selected format, extent and present mode.
    bool CreateRenderPass();                     ///< Create a single-color-attachment render pass.
    bool CreateFramebuffers();                   ///< Create one framebuffer per swapchain image.
    bool CreateCommandPool();                    ///< Create a command pool supporting command-buffer reset.
    bool CreateCommandBuffers();                 ///< Allocate primary command buffers.
    bool CreateSyncObjects();                    ///< Create fences and semaphores.
    bool CreateDescriptorPool();                 ///< Create the ImGui descriptor pool.
    bool InitImGuiVulkan();                      ///< Initialize the ImGui Vulkan renderer.

    /// Destroy framebuffers, views, render pass and swapchain.
    void CleanupSwapchain();

    // Core Vulkan objects.
    VkInstance       instance_       = VK_NULL_HANDLE; ///< Vulkan instance.
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE; ///< Selected physical device.
    VkDevice         device_         = VK_NULL_HANDLE; ///< Logical device.
    VkQueue          graphicsQueue_  = VK_NULL_HANDLE; ///< Graphics/presentation queue.
    uint32_t         queueFamily_    = UINT32_MAX;     ///< Queue-family index.
    VkSurfaceKHR     surface_        = VK_NULL_HANDLE; ///< Android window surface.
    VkSwapchainKHR   swapchain_      = VK_NULL_HANDLE; ///< Swapchain.
    VkRenderPass     renderPass_     = VK_NULL_HANDLE; ///< Render pass.
    VkCommandPool    commandPool_    = VK_NULL_HANDLE; ///< Command pool.
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE; ///< Descriptor pool for ImGui textures.

    // Swapchain resources.
    std::vector<VkImage>         swapchainImages_;      ///< Swapchain image handles.
    std::vector<VkImageView>     swapchainImageViews_;  ///< Swapchain image views.
    std::vector<VkFramebuffer>   framebuffers_;         ///< One framebuffer per image.
    std::vector<VkCommandBuffer> commandBuffers_;       ///< One command buffer per frame slot.

    // Per-frame synchronization objects.
    std::vector<VkFence>         inFlightFences_;           ///< CPU/GPU fence.
    std::vector<VkSemaphore>     imageAvailableSemaphores_; ///< Image-available semaphore.
    std::vector<VkSemaphore>     renderFinishedSemaphores_; ///< Render-finished semaphore.

    // Swapchain parameters.
    VkFormat     swapchainFormat_ = VK_FORMAT_UNDEFINED; ///< Swapchain image format.
    VkExtent2D   swapchainExtent_ = {};                  ///< Swapchain extent.
    uint32_t     imageCount_      = 0;                   ///< Swapchain image count.

    // Frame state.
    uint32_t     currentFrame_       = 0;     ///< Current frame slot in [0, MAX_FRAMES_IN_FLIGHT).
    uint32_t     acquiredImageIndex_ = 0;     ///< Image index acquired for this frame.
    int          width_  = 0;                 ///< Window width.
    int          height_ = 0;                 ///< Window height.
    bool         initialized_  = false;       ///< Initialization state.
    bool         frameStarted_ = false;       ///< Whether this frame has begun successfully.
};
