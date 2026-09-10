/**
 * Vulkan implementation of IGraphicsBackend. Owns instance, surface, device and swapchain
 * resources, drives ImGui through BeginFrame/EndFrame, and releases resources in Shutdown. Device
 * execution and presentation synchronization require separate validation.
 *
 *
 *
 *
 *
 *
 *
 */

#include "VulkanBackend.h"

#include <android/native_window.h>
#include <algorithm>
#include <cstring>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include "Logger.h"

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

/**
 * Log a non-success Vulkan result and return false. Use only inside bool-returning functions.
 *
 *
 *
 */
#define VK_CHECK(expr)                                                     \
    do {                                                                   \
        VkResult _r = (expr);                                              \
        if (_r != VK_SUCCESS) {                                            \
            LOGE("[VulkanBackend] %s failed: %d", #expr, (int)_r);         \
            return false;                                                  \
        }                                                                  \
    } while (0)

/**
 * Error callback supplied through ImGui_ImplVulkan_InitInfo::CheckVkResultFn.
 *
 *
 *
 */
static void CheckVkResult(VkResult err)
{
    if (err != VK_SUCCESS)
        LOGE("[VulkanBackend] VkResult = %d", (int)err);
}

// ===========================================================================
// Initialization entry point.
// ===========================================================================

/**
 * Initialize the instance, surface, device, swapchain, render pass, framebuffers, command
 * resources, synchronization and ImGui renderer in dependency order. On failure, invoke Shutdown.
 * window is the native window; width and height are requested pixel dimensions.
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */
bool VulkanBackend::Init(ANativeWindow *window, int width, int height)
{
    width_  = width;
    height_ = height;

    // Stop at the first failed initialization stage.
    bool ok = CreateInstance()
           && CreateSurface(window)
           && SelectPhysicalDevice()
           && CreateLogicalDevice()
           && CreateSwapchain(width, height)
           && CreateRenderPass()
           && CreateFramebuffers()
           && CreateCommandPool()
           && CreateCommandBuffers()
           && CreateSyncObjects()
           && CreateDescriptorPool()
           && InitImGuiVulkan();

    if (!ok)
    {
        Shutdown();  // Release resources created before failure.
        return false;
    }

    initialized_ = true;
    LOGI("[VulkanBackend] initialized %dx%d  images=%u", width_, height_, imageCount_);
    return true;
}

// ===========================================================================
// Per-frame rendering.
// ===========================================================================

/**
 * Wait for the current slot's fence, acquire a swapchain image and prepare ImGui. If acquisition
 * fails, frameStarted_ remains false and EndFrame skips submission. A submission fence alone does
 * not prove completion of a presentation wait.
 *
 *
 *
 *
 *
 *
 */
void VulkanBackend::BeginFrame()
{
    frameStarted_ = false;

    // Wait for the GPU work previously using this frame slot.
    vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

    // Acquire the next swapchain image.
    VkResult result = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &acquiredImageIndex_);

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        LOGE("[VulkanBackend] vkAcquireNextImageKHR failed: %d", (int)result);
        return;  // frameStarted_ remains false, so EndFrame skips submission.
    }

    // Reset the fence for the next submission.
    vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);
    frameStarted_ = true;

    // Prepare the ImGui Vulkan renderer for a new frame.
    ImGui_ImplVulkan_NewFrame();
}

/**
 * Record ImGui commands into a transparent-clearing render pass, submit with imageAvailable as a
 * wait and renderFinished as a signal, then present and advance the frame slot. Presentation-
 * completion semaphore reuse remains unresolved.
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */
void VulkanBackend::EndFrame()
{
    if (!frameStarted_)
        return;

    ImDrawData *drawData = ImGui::GetDrawData();
    if (!drawData)
        return;

    // Record commands.
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // Re-record for each frame.
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Begin the render pass with transparent clear color.
    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // Transparent RGBA = (0, 0, 0, 0).

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass        = renderPass_;
    rpBegin.framebuffer       = framebuffers_[acquiredImageIndex_];
    rpBegin.renderArea.extent = swapchainExtent_;
    rpBegin.clearValueCount   = 1;
    rpBegin.pClearValues      = &clearValue;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // Record ImGui draw data.
    ImGui_ImplVulkan_RenderDrawData(drawData, cmd);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // Submit the command buffer.
    // Wait for imageAvailable at COLOR_ATTACHMENT_OUTPUT.
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &imageAvailableSemaphores_[currentFrame_];
    submitInfo.pWaitDstStageMask    = &waitStage;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &renderFinishedSemaphores_[currentFrame_];

    // Submit to the graphics queue and signal inFlightFence on completion.
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]);

    // Present.
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderFinishedSemaphores_[currentFrame_]; // Wait for render completion.
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &swapchain_;
    presentInfo.pImageIndices      = &acquiredImageIndex_;

    vkQueuePresentKHR(graphicsQueue_, &presentInfo);

    // Advance the frame slot modulo MAX_FRAMES_IN_FLIGHT.
    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ===========================================================================
// Release Vulkan resources.
// ===========================================================================

/**
 * Wait for device idle, shut down ImGui, destroy synchronization and swapchain resources, then
 * release pools, device, surface and instance. Check handles before destruction. This ordering
 * does not establish caller-side thread safety.
 *
 *
 *
 *
 *
 *
 *
 */
void VulkanBackend::Shutdown()
{
    if (device_)                                                           // Wait for device work to complete.
        vkDeviceWaitIdle(device_);

    if (initialized_)                                                       // Shut down the ImGui Vulkan renderer.
        ImGui_ImplVulkan_Shutdown();

    // Destroy each frame slot's fence and two semaphores.
    for (auto f : inFlightFences_)
        if (f) vkDestroyFence(device_, f, nullptr);
    for (auto s : imageAvailableSemaphores_)
        if (s) vkDestroySemaphore(device_, s, nullptr);
    for (auto s : renderFinishedSemaphores_)
        if (s) vkDestroySemaphore(device_, s, nullptr);
    inFlightFences_.clear();
    imageAvailableSemaphores_.clear();
    renderFinishedSemaphores_.clear();

    CleanupSwapchain();                                                    // Release the swapchain and associated resources.

    // Release remaining objects in dependency order.
    if (descriptorPool_) { vkDestroyDescriptorPool(device_, descriptorPool_, nullptr); descriptorPool_ = VK_NULL_HANDLE; }
    if (commandPool_)    { vkDestroyCommandPool(device_, commandPool_, nullptr);       commandPool_    = VK_NULL_HANDLE; }
    if (device_)         { vkDestroyDevice(device_, nullptr);                          device_         = VK_NULL_HANDLE; }
    if (surface_)        { vkDestroySurfaceKHR(instance_, surface_, nullptr);           surface_        = VK_NULL_HANDLE; }
    if (instance_)       { vkDestroyInstance(instance_, nullptr);                       instance_       = VK_NULL_HANDLE; }

    physicalDevice_ = VK_NULL_HANDLE;
    initialized_    = false;

    LOGI("[VulkanBackend] shutdown");
}

bool VulkanBackend::IsReady() const
{
    return initialized_;
}

// ===========================================================================
// Create the Vulkan instance.
// ===========================================================================

/**
 * Enable VK_KHR_surface and VK_KHR_android_surface and request Vulkan 1.0.
 *
 *
 *
 *
 */
bool VulkanBackend::CreateInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "ImGuiOverlay";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "NoEngine";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_0;  // Request Vulkan 1.0.

    // Required surface extensions.
    const char *extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,          // Generic surface extension.
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,  // Android surface extension.
    };

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &appInfo;
    ci.enabledExtensionCount   = 2;
    ci.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateInstance(&ci, nullptr, &instance_));
    LOGI("[VulkanBackend] instance created");
    return true;
}

// ===========================================================================
// Create the Android Vulkan surface.
// ===========================================================================

/**
 * Create VkSurfaceKHR from the supplied ANativeWindow.
 *
 *
 *
 *
 *
 */
bool VulkanBackend::CreateSurface(ANativeWindow *window)
{
    VkAndroidSurfaceCreateInfoKHR ci{};
    ci.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    ci.window = window;

    VK_CHECK(vkCreateAndroidSurfaceKHR(instance_, &ci, nullptr, &surface_));
    LOGI("[VulkanBackend] surface created");
    return true;
}

// ===========================================================================
// Select a physical device.
// ===========================================================================

/**
 * Select the first device with a queue family supporting graphics and presentation to this
 * surface, and with VK_KHR_swapchain support. Record the device and queue-family index.
 *
 *
 *
 *
 *
 *
 *
 */
bool VulkanBackend::SelectPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) { LOGE("[VulkanBackend] no GPU found"); return false; }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    for (auto dev : devices)
    {
        // Search queue families for combined graphics and presentation support.
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfProps(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount, qfProps.data());

        for (uint32_t i = 0; i < qfCount; ++i)
        {
            // Check graphics support.
            if (!(qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                continue;

            // Check presentation support for this surface.
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &presentSupport);
            if (!presentSupport)
                continue;

            // Check VK_KHR_swapchain support.
            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> exts(extCount);
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());

            bool hasSwapchain = false;
            for (auto &e : exts)
                if (strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                { hasSwapchain = true; break; }

            if (!hasSwapchain)
                continue;

            physicalDevice_ = dev;
            queueFamily_    = i;

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);
            LOGI("[VulkanBackend] GPU: %s  queueFamily=%u", props.deviceName, i);
            return true;
        }
    }

    LOGE("[VulkanBackend] no suitable GPU found");
    return false;
}

// ===========================================================================
// Create the logical device.
// ===========================================================================

/**
 * Enable VK_KHR_swapchain and request one queue from the selected combined graphics/presentation
 * family.
 *
 *
 */
bool VulkanBackend::CreateLogicalDevice()
{
    float priority = 1.0f;  // Queue priority in [0.0, 1.0].

    // Use one queue for graphics and presentation.
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = queueFamily_;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;

    const char *devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME }; // Enable the swapchain extension.

    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = 1;
    dci.ppEnabledExtensionNames = devExts;

    VK_CHECK(vkCreateDevice(physicalDevice_, &dci, nullptr, &device_));
    vkGetDeviceQueue(device_, queueFamily_, 0, &graphicsQueue_);

    LOGI("[VulkanBackend] device created");
    return true;
}

// ===========================================================================
// Create the swapchain.
// ===========================================================================

/**
 * Choose the image format, supported extent, image count, composite alpha and present mode, then
 * create image views. The requested dimensions are constrained by surface capabilities;
 * minImageCount + 1 does not imply a fixed image count.
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */
bool VulkanBackend::CreateSwapchain(int width, int height)
{
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps));

    // Prefer R8G8B8A8_UNORM.
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, formats.data());

    VkSurfaceFormatKHR selectedFmt = formats[0];
    for (auto &f : formats)
    {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM)
        { selectedFmt = f; break; }
    }
    swapchainFormat_ = selectedFmt.format;

    // Select the swapchain extent.
    if (caps.currentExtent.width != UINT32_MAX)
    {
        swapchainExtent_ = caps.currentExtent;
    }
    else
    {
        swapchainExtent_.width  = std::clamp(static_cast<uint32_t>(width),
                                             caps.minImageExtent.width,  caps.maxImageExtent.width);
        swapchainExtent_.height = std::clamp(static_cast<uint32_t>(height),
                                             caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    // Request minImageCount + 1, subject to the surface maximum.
    imageCount_ = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount_ > caps.maxImageCount)
        imageCount_ = caps.maxImageCount;

    // Choose composite alpha for overlay rendering.
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR))
    {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
            compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
            compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        else
            compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }

    // Prefer MAILBOX; otherwise use FIFO.
    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &pmCount, presentModes.data());

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : presentModes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = m; break; }

    // Create the swapchain.
    VkSwapchainCreateInfoKHR sci{};
    sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface          = surface_;
    sci.minImageCount    = imageCount_;
    sci.imageFormat      = selectedFmt.format;
    sci.imageColorSpace  = selectedFmt.colorSpace;
    sci.imageExtent      = swapchainExtent_;
    sci.imageArrayLayers = 1;                                  // One image layer.
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // Color-attachment usage.
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;           // Exclusive sharing for one queue family.
    sci.preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sci.compositeAlpha   = compositeAlpha;
    sci.presentMode      = presentMode;
    sci.clipped          = VK_TRUE;                             // Allow clipping of obscured pixels.

    VK_CHECK(vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain_));

    // Get swapchain image handles.
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount_, nullptr);
    swapchainImages_.resize(imageCount_);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount_, swapchainImages_.data());

    // Create one image view per image.
    swapchainImageViews_.resize(imageCount_);
    for (uint32_t i = 0; i < imageCount_; ++i)
    {
        VkImageViewCreateInfo vci{};
        vci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image    = swapchainImages_[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format   = swapchainFormat_;
        vci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        VK_CHECK(vkCreateImageView(device_, &vci, nullptr, &swapchainImageViews_[i]));
    }

    LOGI("[VulkanBackend] swapchain %ux%u  images=%u  fmt=%d",
         swapchainExtent_.width, swapchainExtent_.height, imageCount_, (int)swapchainFormat_);
    return true;
}

// ===========================================================================
// Create the render pass.
// ===========================================================================

/**
 * Use one color attachment with CLEAR/STORE and UNDEFINED-to-PRESENT_SRC layout transition. The
 * external subpass dependency uses COLOR_ATTACHMENT_OUTPUT. This does not replace required
 * queue/presentation synchronization.
 *
 *
 *
 *
 *
 *
 */
bool VulkanBackend::CreateRenderPass()
{
    VkAttachmentDescription att{};
    att.format         = swapchainFormat_;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;         // Single-sample rendering.
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;    // Clear on load.
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;   // Store after rendering.
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;      // Discard initial contents.
    att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Final layout for presentation.

    VkAttachmentReference ref{};
    ref.attachment = 0;
    ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &ref;

    // External-to-subpass dependency at COLOR_ATTACHMENT_OUTPUT.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &att;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;

    VK_CHECK(vkCreateRenderPass(device_, &rpci, nullptr, &renderPass_));
    return true;
}

// ===========================================================================
// Create framebuffers.
// ===========================================================================

/**
 * Create one framebuffer per swapchain image view; acquiredImageIndex_ selects the frame target.
 *
 *
 *
 */
bool VulkanBackend::CreateFramebuffers()
{
    framebuffers_.resize(imageCount_);
    for (uint32_t i = 0; i < imageCount_; ++i)
    {
        VkFramebufferCreateInfo fci{};
        fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass      = renderPass_;
        fci.attachmentCount = 1;
        fci.pAttachments    = &swapchainImageViews_[i];
        fci.width           = swapchainExtent_.width;
        fci.height          = swapchainExtent_.height;
        fci.layers          = 1;

        VK_CHECK(vkCreateFramebuffer(device_, &fci, nullptr, &framebuffers_[i]));
    }
    return true;
}

// ===========================================================================
// Create the command pool.
// ===========================================================================

/**
 * RESET_COMMAND_BUFFER_BIT allows individual command buffers to be re-recorded without rebuilding
 * the pool.
 *
 *
 */
bool VulkanBackend::CreateCommandPool()
{
    VkCommandPoolCreateInfo cpci{};
    cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // Allow individual command-buffer reset.
    cpci.queueFamilyIndex = queueFamily_;  // Use the graphics queue family.

    VK_CHECK(vkCreateCommandPool(device_, &cpci, nullptr, &commandPool_));
    return true;
}

// ===========================================================================
// Allocate command buffers.
// ===========================================================================

/**
 * Allocate one primary command buffer per frame slot for direct queue submission.
 *
 *
 *
 */
bool VulkanBackend::CreateCommandBuffers()
{
    commandBuffers_.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = commandPool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    VK_CHECK(vkAllocateCommandBuffers(device_, &ai, commandBuffers_.data()));
    return true;
}

// ===========================================================================
// Create synchronization objects.
// ===========================================================================

/**
 * Each frame slot owns an initially signaled fence, image-available semaphore and render-finished
 * semaphore. Submission-fence completion does not by itself make a presentation-wait semaphore
 * reusable.
 *
 *
 *
 */
bool VulkanBackend::CreateSyncObjects()
{
    inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);
    imageAvailableSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Initially signaled so the first frame can proceed.

    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        VK_CHECK(vkCreateFence(device_, &fci, nullptr, &inFlightFences_[i]));
        VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr, &imageAvailableSemaphores_[i]));
        VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr, &renderFinishedSemaphores_[i]));
    }
    return true;
}

// ===========================================================================
// Create the descriptor pool.
// ===========================================================================

/**
 * Allocate 16 COMBINED_IMAGE_SAMPLER descriptors and allow individual descriptor-set freeing.
 * Callers must remain within the pool capacity.
 *
 *
 *
 */
bool VulkanBackend::CreateDescriptorPool()
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 16;

    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets       = 16;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &poolSize;

    VK_CHECK(vkCreateDescriptorPool(device_, &dpci, nullptr, &descriptorPool_));
    return true;
}

// ===========================================================================
// Initialize the ImGui Vulkan renderer.
// ===========================================================================

/**
 * Pass the Vulkan objects to ImGui for pipeline, font and draw-command resources. The ImGui
 * context must already exist.
 *
 *
 *
 *
 *
 *
 */
bool VulkanBackend::InitImGuiVulkan()
{
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion      = VK_API_VERSION_1_0;
    info.Instance        = instance_;
    info.PhysicalDevice  = physicalDevice_;
    info.Device          = device_;
    info.QueueFamily     = queueFamily_;
    info.Queue           = graphicsQueue_;
    info.DescriptorPool  = descriptorPool_;
    info.RenderPass      = renderPass_;
    info.MinImageCount   = imageCount_;
    info.ImageCount      = imageCount_;
    info.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
    info.PipelineCache   = VK_NULL_HANDLE;
    info.Subpass         = 0;
    info.CheckVkResultFn = CheckVkResult;

    if (!ImGui_ImplVulkan_Init(&info))
    {
        LOGE("[VulkanBackend] ImGui_ImplVulkan_Init failed");
        return false;
    }

    LOGI("[VulkanBackend] ImGui Vulkan backend initialized");
    return true;
}

// ---------------------------------------------------------------------------
// ===========================================================================
// Swapchain cleanup.
// ===========================================================================

/**
 * Destroy framebuffers, image views, render pass and swapchain in that order.
 *
 *
 *
 */
void VulkanBackend::CleanupSwapchain()
{
    for (auto fb : framebuffers_)
        if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();

    for (auto view : swapchainImageViews_)
        if (view) vkDestroyImageView(device_, view, nullptr);
    swapchainImageViews_.clear();

    swapchainImages_.clear();

    if (renderPass_) { vkDestroyRenderPass(device_, renderPass_, nullptr); renderPass_ = VK_NULL_HANDLE; }
    if (swapchain_)  { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
}
