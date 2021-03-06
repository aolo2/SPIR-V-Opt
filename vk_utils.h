static u32 *
get_binary(const char *filename, u32 *size)
{
    FILE *file = fopen(filename, "rb");
    
    if (!file) {
        printf("[ERROR] File could not be opened\n");
        return(NULL);
    }
    
    fseek(file, 0L, SEEK_END);
    *size = ftell(file);
    rewind(file);
    
    // NOTE: size % sizeof(u32) is always zero
    u32 *buffer = (u32 *) malloc(*size);
    fread((u8 *) buffer, *size, 1, file);
    
    *size /= sizeof(u32);
    
    fclose(file);
    
    return(buffer);
}


static void
rebuild_fragment_shader()
{
    VkShaderModuleCreateInfo module_create_info;
    u32 *fs_words;
    u32 fs_size;
    
    fs_words = get_binary("shaders/sample.frag.spv", &fs_size);
    
    module_create_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_create_info.pNext    = NULL;
    module_create_info.flags    = 0;
    module_create_info.codeSize = fs_size * sizeof(u32);
    module_create_info.pCode    = fs_words;
    
    ASSERT_VK(vkCreateShaderModule(data.device, &module_create_info, NULL, &data.shader_stages[1].module));
}

static void
update_uniform_data(u32 mem_reqs_size)
{
    ASSERT_VK(vkMapMemory(data.device, data.uniform.mem, 0, mem_reqs_size, 0, (void **) &data.ubuffer_data));
    memcpy(data.ubuffer_data, data.mvp, sizeof(data.mvp));
    vkUnmapMemory(data.device, data.uniform.mem);
    ASSERT_VK(vkBindBufferMemory(data.device, data.uniform.buf, data.uniform.mem, 0));
}

static void
create_xcb_window(u32 width, u32 height)
{
    // init connection
    s32 screen;
    const xcb_setup_t *setup;
    xcb_screen_iterator_t si;
    
    data.connection = xcb_connect(NULL, &screen);
    setup = xcb_get_setup(data.connection);
    si = xcb_setup_roots_iterator(setup);
    
    while (screen-- > 0) {
        xcb_screen_next(&si);
    }
    
    data.screen = si.data;
    
    // init window
    u32 value_mask;
    u32 value_list[32];
    
    data.width = width;
    data.height = height;
    
    data.window = xcb_generate_id(data.connection);
    
    value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    value_list[0] = data.screen->black_pixel;
    value_list[1] = XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_EXPOSURE;
    
    xcb_create_window(data.connection, XCB_COPY_FROM_PARENT, data.window, data.screen->root, 0, 0, data.width, data.height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, data.screen->root_visual, value_mask, value_list);
    
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(data.connection, 1, 12, "WM_PROTOCOLS");
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(data.connection, cookie, 0);
    
    xcb_intern_atom_cookie_t cookie2 = xcb_intern_atom(data.connection, 0, 16, "WM_DELETE_WINDOW");
    data.atom_wm_delete_window = xcb_intern_atom_reply(data.connection, cookie2, 0);
    
    xcb_change_property(data.connection, XCB_PROP_MODE_REPLACE, data.window, (*reply).atom, 4, 32, 1,
                        &(*data.atom_wm_delete_window).atom);
    
    xcb_map_window(data.connection, data.window);
    
    const u32 coords[2] = {100, 100};
    xcb_configure_window(data.connection, data.window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, coords);
    xcb_flush(data.connection);
    
    xcb_generic_event_t *e;
    while ((e = xcb_wait_for_event(data.connection))) {
        if ((e->response_type & ~0x80) == XCB_EXPOSE)  {
            break;
        }
    }
}

static bool
find_graphics_and_present_queues()
{
    VkBool32 *supports_present = malloc(data.queue_family_count * sizeof(VkBool32));
    
    data.graphics_queue_family_index = UINT32_MAX;
    data.present_queue_family_index = UINT32_MAX;
    
    for (u32 i = 0; i < data.queue_family_count; ++i) {
        vkGetPhysicalDeviceSurfaceSupportKHR(data.gpus[0], i, data.surface, supports_present + i);
    }
    
    for (u32 i = 0; i < data.queue_family_count; ++i) {
        if ((data.queue_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            if (data.graphics_queue_family_index == UINT32_MAX) {
                data.graphics_queue_family_index = i;
            }
            
            if (supports_present[i] == VK_TRUE) {
                data.graphics_queue_family_index = i;
                data.present_queue_family_index = i;
                break;
            }
        }
    }
    
    if (data.present_queue_family_index == UINT32_MAX) {
        for (u32 i = 0; i < data.queue_family_count; ++i) {
            if (supports_present[i] == VK_TRUE) {
                data.present_queue_family_index = i;
                break;
            }
        }
    }
    
    return(data.present_queue_family_index != UINT32_MAX && 
           data.graphics_queue_family_index != UINT32_MAX);
}

static bool
memory_type_from_properties(u32 type_bits, VkFlags requirements_mask, u32 *type_index) {
    for (u32 i = 0; i < data.memory_properties.memoryTypeCount; ++i) {
        if ((type_bits & 1) == 1) {
            if ((data.memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask) {
                *type_index = i;
                return(true);
            }
        }
        type_bits = type_bits >> 1;
    }
    return(false);
}

static void
init_instance()
{
    u32 extension_count = 2;
    const char **instance_extension_names;
    VkApplicationInfo application_info;
    VkInstanceCreateInfo instance_create_info;
    
    application_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.pNext              = NULL;
    application_info.pApplicationName   = "thesis";
    application_info.applicationVersion = 1;
    application_info.pEngineName        = "thesis";
    application_info.engineVersion      = 1;
    application_info.apiVersion         = VK_API_VERSION_1_0;
    
    ASSERT(instance_extension_names = malloc(extension_count * sizeof(char *)));
    ASSERT(instance_extension_names[0] = strdup(VK_KHR_SURFACE_EXTENSION_NAME));
    ASSERT(instance_extension_names[1] = strdup(VK_KHR_XCB_SURFACE_EXTENSION_NAME));
    
    instance_create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.pNext                   = NULL;
    instance_create_info.flags                   = 0;
    instance_create_info.pApplicationInfo        = &application_info;
    instance_create_info.ppEnabledExtensionNames = instance_extension_names;
    instance_create_info.enabledExtensionCount   = extension_count;
    instance_create_info.enabledLayerCount       = 0;
    instance_create_info.ppEnabledLayerNames     = NULL;
    
    ASSERT_VK(vkCreateInstance(&instance_create_info, NULL, &data.instance));
}

static void
enumerate_devices()
{
    ASSERT_VK(vkEnumeratePhysicalDevices(data.instance, &data.gpu_count, NULL));
    ASSERT(data.gpu_count);
    ASSERT(data.gpus = malloc(data.gpu_count * sizeof(VkPhysicalDevice)));
    ASSERT_VK(vkEnumeratePhysicalDevices(data.instance, &data.gpu_count, data.gpus));
    
    vkGetPhysicalDeviceQueueFamilyProperties(data.gpus[0], &data.queue_family_count, NULL);
    ASSERT(data.queue_family_count);
    ASSERT(data.queue_properties = malloc(data.queue_family_count * sizeof(VkQueueFamilyProperties)));
    vkGetPhysicalDeviceQueueFamilyProperties(data.gpus[0], &data.queue_family_count, data.queue_properties);
    
    vkGetPhysicalDeviceMemoryProperties(data.gpus[0], &data.memory_properties);
    vkGetPhysicalDeviceProperties(data.gpus[0], &data.gpu_props);
}

static void
init_surface()
{
    u32 format_count;
    VkXcbSurfaceCreateInfoKHR surface_create_info;
    VkSurfaceFormatKHR *surf_formats;
    
    create_xcb_window(800, 600); // this inits data.connection and data.window
    
    surface_create_info.sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    surface_create_info.pNext      = NULL;
    surface_create_info.connection = data.connection;
    surface_create_info.window     = data.window;
    
    ASSERT_VK(vkCreateXcbSurfaceKHR(data.instance, &surface_create_info, NULL, &data.surface));
    ASSERT(find_graphics_and_present_queues());
    
    ASSERT_VK(vkGetPhysicalDeviceSurfaceFormatsKHR(data.gpus[0], data.surface, &format_count, NULL));
    ASSERT(surf_formats = malloc(format_count * sizeof(VkSurfaceFormatKHR)));
    ASSERT_VK(vkGetPhysicalDeviceSurfaceFormatsKHR(data.gpus[0], data.surface, &format_count, surf_formats));
    
    if (format_count == 1 && surf_formats[0].format == VK_FORMAT_UNDEFINED) {
        data.format = VK_FORMAT_B8G8R8A8_UNORM;
    } else {
        data.format = surf_formats[0].format;
    }
}

static void
init_device()
{
    u32 extension_count = 1;
    f32 queue_priorities[1] = { 0.0f };
    VkDeviceQueueCreateInfo queue_info;
    
    queue_info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.pNext            = NULL;
    queue_info.queueCount       = 1;
    queue_info.pQueuePriorities = queue_priorities;
    queue_info.queueFamilyIndex = data.graphics_queue_family_index;
    
    ASSERT(data.device_extension_names = malloc(extension_count * sizeof(char *)));
    ASSERT(data.device_extension_names[0] = strdup(VK_KHR_SWAPCHAIN_EXTENSION_NAME));
    
    data.device_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    data.device_info.pNext                   = NULL;
    data.device_info.queueCreateInfoCount    = 1;
    data.device_info.pQueueCreateInfos       = &queue_info;
    data.device_info.ppEnabledExtensionNames = data.device_extension_names;
    data.device_info.enabledExtensionCount   = extension_count;
    data.device_info.enabledLayerCount       = 0;
    data.device_info.ppEnabledLayerNames     = NULL;
    data.device_info.pEnabledFeatures        = NULL;
    
    ASSERT_VK(vkCreateDevice(data.gpus[0], &data.device_info, NULL, &data.device));
}

static void
init_command_buffer()
{
    VkCommandBufferAllocateInfo cmd;
    VkCommandPoolCreateInfo cbp_info;
    
    cbp_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cbp_info.pNext            = NULL;
    cbp_info.queueFamilyIndex = data.graphics_queue_family_index;
    cbp_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    ASSERT_VK(vkCreateCommandPool(data.device, &cbp_info, NULL, &data.cbp));
    
    cmd.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd.pNext              = NULL;
    cmd.commandPool        = data.cbp;
    cmd.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd.commandBufferCount = 1;
    
    ASSERT_VK(vkAllocateCommandBuffers(data.device, &cmd, &data.command_buffer));
    
    // Begin command buffer
    {
        data.cmd_buf_info.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        data.cmd_buf_info.pNext            = NULL;
        data.cmd_buf_info.flags            = 0;
        data.cmd_buf_info.pInheritanceInfo = NULL;
        
        ASSERT_VK(vkBeginCommandBuffer(data.command_buffer, &data.cmd_buf_info));
    } // End of begin command buffer
    
    // Init device queue
    {
        vkGetDeviceQueue(data.device, data.graphics_queue_family_index, 0, &data.graphics_queue);
        if (data.graphics_queue_family_index == data.present_queue_family_index) {
            data.present_queue = data.graphics_queue;
        } else {
            vkGetDeviceQueue(data.device, data.present_queue_family_index, 0, &data.present_queue);
        }
    } // End of init device queue
}

static void
init_swapchain()
{
    u32 present_mode_count;
    VkSurfaceCapabilitiesKHR capabilities;
    VkPresentModeKHR *present_modes;
    VkExtent2D swapchain_extent;
    VkSurfaceTransformFlagBitsKHR pre_transform;
    VkCompositeAlphaFlagBitsKHR composite_alpha;
    VkSwapchainCreateInfoKHR swapchain_create_info;
    
    VkCompositeAlphaFlagBitsKHR composite_alpha_flags[4] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    
    u32 queue_family_indices[2] = {
        (u32) data.graphics_queue_family_index, 
        (u32) data.present_queue_family_index
    };
    
    ASSERT_VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(data.gpus[0], data.surface, &capabilities));
    ASSERT_VK(vkGetPhysicalDeviceSurfacePresentModesKHR(data.gpus[0], data.surface, &present_mode_count, NULL));
    ASSERT(present_modes = malloc(present_mode_count * sizeof(VkPresentModeKHR)));
    ASSERT_VK(vkGetPhysicalDeviceSurfacePresentModesKHR(data.gpus[0], data.surface, &present_mode_count, present_modes));
    
    if (capabilities.currentExtent.width == 0xFFFFFFFF) {
        swapchain_extent.width = data.width;
        swapchain_extent.height = data.height;
        
        if (swapchain_extent.width < capabilities.minImageExtent.width) {
            swapchain_extent.width = capabilities.minImageExtent.width;
        } else if (swapchain_extent.width > capabilities.maxImageExtent.width) {
            swapchain_extent.width = capabilities.maxImageExtent.width;
        }
        
        if (swapchain_extent.height < capabilities.minImageExtent.height) {
            swapchain_extent.height = capabilities.minImageExtent.height;
        } else if (swapchain_extent.height > capabilities.maxImageExtent.height) {
            swapchain_extent.height = capabilities.maxImageExtent.height;
        }
    } else {
        swapchain_extent= capabilities.currentExtent;
    }
    
    if (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    } else {
        pre_transform = capabilities.currentTransform;
    }
    
    composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (u32 i = 0; i < 4; ++i) {
        if (capabilities.supportedCompositeAlpha & composite_alpha_flags[i]) {
            composite_alpha = composite_alpha_flags[i];
            break;
        }
    }
    
    swapchain_create_info.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.pNext                 = NULL;
    swapchain_create_info.surface               = data.surface;
    swapchain_create_info.minImageCount         = capabilities.minImageCount;
    swapchain_create_info.imageFormat           = data.format;
    swapchain_create_info.imageExtent.width     = swapchain_extent.width;
    swapchain_create_info.imageExtent.height    = swapchain_extent.height;
    swapchain_create_info.preTransform          = pre_transform;
    swapchain_create_info.compositeAlpha        = composite_alpha;
    swapchain_create_info.imageArrayLayers      = 1;
    swapchain_create_info.presentMode           = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_create_info.oldSwapchain          = VK_NULL_HANDLE;
    swapchain_create_info.clipped               = true;
    swapchain_create_info.imageColorSpace       = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    swapchain_create_info.imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swapchain_create_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.queueFamilyIndexCount = 0;
    swapchain_create_info.pQueueFamilyIndices   = NULL;
    
    if (data.graphics_queue_family_index != data.present_queue_family_index) {
        swapchain_create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        swapchain_create_info.queueFamilyIndexCount = 2;
        swapchain_create_info.pQueueFamilyIndices   = queue_family_indices;
    }
    
    ASSERT_VK(vkCreateSwapchainKHR(data.device, &swapchain_create_info, NULL, &data.swapchain));
    ASSERT_VK(vkGetSwapchainImagesKHR(data.device, data.swapchain, &data.swapchain_image_count, NULL));
    ASSERT(data.swapchain_images = malloc(data.swapchain_image_count * sizeof(VkImage)));
    ASSERT_VK(vkGetSwapchainImagesKHR(data.device, data.swapchain, &data.swapchain_image_count, data.swapchain_images));
    ASSERT(data.buffers = malloc(data.swapchain_image_count * sizeof(struct swapchain_buffer)));
    
    struct swapchain_buffer *sc_head = data.buffers;
    
    for (u32 i = 0; i < data.swapchain_image_count; ++i) {
        VkImageViewCreateInfo color_image_view;
        struct swapchain_buffer sc_buffer;
        
        data.buffers[i].image = data.swapchain_images[i];
        
        color_image_view.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        color_image_view.pNext                           = NULL;
        color_image_view.flags                           = 0;
        color_image_view.image                           = data.buffers[i].image;
        color_image_view.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        color_image_view.format                          = data.format;
        color_image_view.components.r                    = VK_COMPONENT_SWIZZLE_R;
        color_image_view.components.g                    = VK_COMPONENT_SWIZZLE_G;
        color_image_view.components.b                    = VK_COMPONENT_SWIZZLE_B;
        color_image_view.components.a                    = VK_COMPONENT_SWIZZLE_A;
        color_image_view.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        color_image_view.subresourceRange.baseMipLevel   = 0;
        color_image_view.subresourceRange.levelCount     = 1;
        color_image_view.subresourceRange.baseArrayLayer = 0;
        color_image_view.subresourceRange.layerCount     = 1;
        color_image_view.image                           = data.swapchain_images[i];
        
        sc_buffer.image = data.swapchain_images[i];
        *sc_head++ = sc_buffer;
        
        ASSERT_VK(vkCreateImageView(data.device, &color_image_view, NULL, &data.buffers[i].view));
    }
    
    data.current_buffer = 0;
}

static void
init_depth_buffer()
{
    VkImageCreateInfo image_info;
    VkFormatProperties properties;
    VkMemoryAllocateInfo mem_alloc;
    VkImageViewCreateInfo view_info;
    VkMemoryRequirements mem_reqs;
    VkFormat depth_format;
    
    if (data.depth.format == VK_FORMAT_UNDEFINED) {
        data.depth.format = VK_FORMAT_D16_UNORM;
    }
    
    depth_format = data.depth.format;
    
    vkGetPhysicalDeviceFormatProperties(data.gpus[0], depth_format, &properties);
    
    if (properties.linearTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        image_info.tiling = VK_IMAGE_TILING_LINEAR;
    } else if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    } else {
        printf("[ERROR] VK_FORMAT_D16_UNORM Unsupported.\n");
        exit(1);
    }
    
    image_info.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext                 = NULL;
    image_info.imageType             = VK_IMAGE_TYPE_2D;
    image_info.format                = depth_format;
    image_info.extent.width          = data.width;
    image_info.extent.height         = data.height;
    image_info.extent.depth          = 1;
    image_info.mipLevels             = 1;
    image_info.arrayLayers           = 1;
    image_info.samples               = NUM_SAMPLES;
    image_info.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage                 = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_info.queueFamilyIndexCount = 0;
    image_info.pQueueFamilyIndices   = NULL;
    image_info.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
    image_info.flags                 = 0;
    
    mem_alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mem_alloc.pNext           = NULL;
    mem_alloc.allocationSize  = 0;
    mem_alloc.memoryTypeIndex = 0;
    
    view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.pNext                           = NULL;
    view_info.image                           = VK_NULL_HANDLE;
    view_info.format                          = depth_format;
    view_info.components.r                    = VK_COMPONENT_SWIZZLE_R;
    view_info.components.g                    = VK_COMPONENT_SWIZZLE_G;
    view_info.components.b                    = VK_COMPONENT_SWIZZLE_B;
    view_info.components.a                    = VK_COMPONENT_SWIZZLE_A;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.flags                           = 0;
    
    if (depth_format == VK_FORMAT_D16_UNORM_S8_UINT || depth_format == VK_FORMAT_D24_UNORM_S8_UINT || depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        view_info.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    
    ASSERT_VK(vkCreateImage(data.device, &image_info, NULL, &data.depth.image));
    
    vkGetImageMemoryRequirements(data.device, data.depth.image, &mem_reqs);
    
    mem_alloc.allocationSize = mem_reqs.size;
    
    ASSERT(memory_type_from_properties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mem_alloc.memoryTypeIndex));
    ASSERT_VK(vkAllocateMemory(data.device, &mem_alloc, NULL, &data.depth.mem));
    ASSERT_VK(vkBindImageMemory(data.device, data.depth.image, data.depth.mem, 0));
    
    view_info.image = data.depth.image;
    
    ASSERT_VK(vkCreateImageView(data.device, &view_info, NULL, &data.depth.view));
}

static void
init_uniform_buffer()
{ 
    VkBufferCreateInfo buf_info;
    VkMemoryAllocateInfo alloc_info;
    
    vec3 eye    = { -5, 3, -10 };
    vec3 center = { 0, 0, 0 };
    vec3 up     = { 0, -1, 0 };
    
    mat4x4_perspective(data.projection, 0.785f, (f32) data.width / (f32) data.height, 0.1f, 100.0f);
    mat4x4_look_at(data.view, eye, center, up);
    mat4x4_identity(data.model);
    mat4x4_identity(data.clip);
    
    data.clip[1][1] = -1.0f;
    data.clip[2][2] = 0.5f;
    data.clip[3][2] = 0.5f;
    
    mat4x4_mul(data.mvp, data.clip, data.projection);
    mat4x4_mul(data.mvp, data.mvp, data.view);
    mat4x4_mul(data.mvp, data.mvp, data.model);
    
    buf_info.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.pNext                 = NULL;
    buf_info.usage                 = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    buf_info.size                  = sizeof(data.mvp);
    buf_info.queueFamilyIndexCount = 0;
    buf_info.pQueueFamilyIndices   = NULL;
    buf_info.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
    buf_info.flags                 = 0;
    
    ASSERT_VK(vkCreateBuffer(data.device, &buf_info, NULL, &data.uniform.buf));
    
    vkGetBufferMemoryRequirements(data.device, data.uniform.buf, &data.umem_reqs);
    
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext           = NULL;
    alloc_info.memoryTypeIndex = 0;
    alloc_info.allocationSize  = data.umem_reqs.size;
    
    ASSERT(memory_type_from_properties(data.umem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &alloc_info.memoryTypeIndex));
    ASSERT_VK(vkAllocateMemory(data.device, &alloc_info, NULL, &(data.uniform.mem)));
    
    update_uniform_data(data.umem_reqs.size);
    
    data.uniform.buffer_info.buffer = data.uniform.buf;
    data.uniform.buffer_info.offset = 0;
    data.uniform.buffer_info.range  = sizeof(data.mvp);
}

static void
init_pipeline_layout()
{
    VkDescriptorSetLayoutBinding layout_binding;
    VkDescriptorSetLayoutCreateInfo descriptor_layout;
    VkPipelineLayoutCreateInfo pipeline_layout_create_info;
    
    layout_binding.binding            = 0;
    layout_binding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layout_binding.descriptorCount    = 1;
    layout_binding.stageFlags         = VK_SHADER_STAGE_VERTEX_BIT;
    layout_binding.pImmutableSamplers = NULL;
    
    descriptor_layout.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_layout.pNext        = NULL;
    descriptor_layout.bindingCount = 1;
    descriptor_layout.pBindings    = &layout_binding;
    descriptor_layout.flags        = 0;
    
    ASSERT_VK(vkCreateDescriptorSetLayout(data.device, &descriptor_layout, NULL, data.descriptor_layout));
    
    pipeline_layout_create_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.pNext                  = NULL;
    pipeline_layout_create_info.pushConstantRangeCount = 0;
    pipeline_layout_create_info.pPushConstantRanges    = NULL;
    pipeline_layout_create_info.setLayoutCount         = NUM_DESCRIPTOR_SETS;
    pipeline_layout_create_info.pSetLayouts            = data.descriptor_layout;
    
    ASSERT_VK(vkCreatePipelineLayout(data.device, &pipeline_layout_create_info, NULL, &data.pipeline_layout));
}

static void
init_render_pass()
{
    VkAttachmentDescription attachments[2];
    VkAttachmentReference color_reference;
    VkAttachmentReference depth_reference;
    VkSubpassDescription subpass;
    VkRenderPassCreateInfo rp_info;
    
    attachments[0].format         = data.format;
    attachments[0].samples        = NUM_SAMPLES;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments[0].flags          = 0;
    
    attachments[1].format         = data.depth.format;
    attachments[1].samples        = NUM_SAMPLES;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].flags          = 0;
    
    color_reference.attachment = 0;
    color_reference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    depth_reference.attachment = 1;
    depth_reference.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.flags                   = 0;
    subpass.inputAttachmentCount    = 0;
    subpass.pInputAttachments       = NULL;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &color_reference;
    subpass.pResolveAttachments     = NULL;
    subpass.pDepthStencilAttachment = &depth_reference;
    subpass.preserveAttachmentCount = 0;
    subpass.pPreserveAttachments    = NULL;
    
    rp_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.pNext           = NULL;
    rp_info.attachmentCount = 2;
    rp_info.pAttachments    = attachments;
    rp_info.subpassCount    = 1;
    rp_info.pSubpasses      = &subpass;
    rp_info.dependencyCount = 0;
    rp_info.pDependencies   = NULL;
    
    ASSERT_VK(vkCreateRenderPass(data.device, &rp_info, NULL, &data.render_pass));
}

static void
init_shaders()
{
    VkShaderModuleCreateInfo module_create_info;
    u32 *vs_words, *fs_words;
    u32 vs_size, fs_size;
    
    vs_words = get_binary("shaders/sample.vert.spv", &vs_size);
    fs_words = get_binary("shaders/sample.frag.spv", &fs_size);
    
    data.shader_stages[0].sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    data.shader_stages[0].pNext               = NULL;
    data.shader_stages[0].pSpecializationInfo = NULL;
    data.shader_stages[0].flags               = 0;
    data.shader_stages[0].stage               = VK_SHADER_STAGE_VERTEX_BIT;
    data.shader_stages[0].pName               = "main";
    
    module_create_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_create_info.pNext    = NULL;
    module_create_info.flags    = 0;
    module_create_info.codeSize = vs_size * sizeof(u32);
    module_create_info.pCode    = vs_words;
    
    ASSERT_VK(vkCreateShaderModule(data.device, &module_create_info, NULL, &data.shader_stages[0].module));
    
    data.shader_stages[1].sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    data.shader_stages[1].pNext               = NULL;
    data.shader_stages[1].pSpecializationInfo = NULL;
    data.shader_stages[1].flags               = 0;
    data.shader_stages[1].stage               = VK_SHADER_STAGE_FRAGMENT_BIT;
    data.shader_stages[1].pName               = "main";
    
    module_create_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_create_info.pNext    = NULL;
    module_create_info.flags    = 0;
    module_create_info.codeSize = fs_size * sizeof(u32);
    module_create_info.pCode    = fs_words;
    
    ASSERT_VK(vkCreateShaderModule(data.device, &module_create_info, NULL, &data.shader_stages[1].module));
}

static void
init_framebuffers()
{
    VkFramebufferCreateInfo fb_info;
    VkImageView attachments[2];
    
    attachments[1] = data.depth.view;
    
    fb_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.pNext           = NULL;
    fb_info.renderPass      = data.render_pass;
    fb_info.attachmentCount = 2;
    fb_info.pAttachments    = attachments;
    fb_info.width           = data.width;
    fb_info.height          = data.height;
    fb_info.layers          = 1;
    
    ASSERT(data.framebuffers = malloc(data.swapchain_image_count * sizeof(VkFramebuffer)));
    
    for (u32 i = 0; i < data.swapchain_image_count; ++i) {
        attachments[0] = data.buffers[i].view;
        ASSERT_VK(vkCreateFramebuffer(data.device, &fb_info, NULL, &data.framebuffers[i]));
    }
}

static void
init_vertex_buffer()
{
    u8 *vertex_data;
    VkBufferCreateInfo buf_info;
    VkMemoryRequirements mem_reqs;
    VkMemoryAllocateInfo alloc_info;
    
    buf_info.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.pNext                 = NULL;
    buf_info.usage                 = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buf_info.size                  = sizeof(g_vb_solid_face_colors_Data);
    buf_info.queueFamilyIndexCount = 0;
    buf_info.pQueueFamilyIndices   = NULL;
    buf_info.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
    buf_info.flags                 = 0;
    
    ASSERT_VK(vkCreateBuffer(data.device, &buf_info, NULL, &data.vertex.buf));
    vkGetBufferMemoryRequirements(data.device, data.vertex.buf, &mem_reqs);
    
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext           = NULL;
    alloc_info.memoryTypeIndex = 0;
    alloc_info.allocationSize  = mem_reqs.size;
    
    ASSERT(memory_type_from_properties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &alloc_info.memoryTypeIndex));
    
    ASSERT_VK(vkAllocateMemory(data.device, &alloc_info, NULL, &(data.vertex.mem)));
    
    data.vertex.buffer_info.range  = mem_reqs.size;
    data.vertex.buffer_info.offset = 0;
    
    ASSERT_VK(vkMapMemory(data.device, data.vertex.mem, 0, mem_reqs.size, 0, (void **) &vertex_data));
    
    memcpy(vertex_data, g_vb_solid_face_colors_Data, sizeof(g_vb_solid_face_colors_Data));
    vkUnmapMemory(data.device, data.vertex.mem);
    ASSERT_VK(vkBindBufferMemory(data.device, data.vertex.buf, data.vertex.mem, 0));
    
    data.vi_binding.binding   = 0;
    data.vi_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    data.vi_binding.stride    = sizeof(g_vb_solid_face_colors_Data[0]);
    
    data.vi_attribs[0].binding  = 0;
    data.vi_attribs[0].location = 0;
    data.vi_attribs[0].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    data.vi_attribs[0].offset   = 0;
    
    data.vi_attribs[1].binding  = 0;
    data.vi_attribs[1].location = 1;
    data.vi_attribs[1].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    data.vi_attribs[1].offset   = 16;
}

static void
init_descriptor_poolset()
{
    VkWriteDescriptorSet writes[1];
    VkDescriptorPoolSize type_count[1];
    VkDescriptorSetAllocateInfo alloc_info[1];
    VkDescriptorPoolCreateInfo descriptor_pool;
    
    type_count[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    type_count[0].descriptorCount = 1;
    
    descriptor_pool.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool.pNext         = NULL;
    descriptor_pool.maxSets       = 1;
    descriptor_pool.poolSizeCount = 1;
    descriptor_pool.pPoolSizes    = type_count;
    
    ASSERT_VK(vkCreateDescriptorPool(data.device, &descriptor_pool, NULL, &data.descriptor_pool));
    
    alloc_info[0].sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info[0].pNext              = NULL;
    alloc_info[0].descriptorPool     = data.descriptor_pool;
    alloc_info[0].descriptorSetCount = NUM_DESCRIPTOR_SETS;
    alloc_info[0].pSetLayouts        = data.descriptor_layout;
    
    ASSERT_VK(vkAllocateDescriptorSets(data.device, alloc_info, data.descriptor_set));
    
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].pNext           = NULL;
    writes[0].dstSet          = data.descriptor_set[0];
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo     = &data.uniform.buffer_info;
    writes[0].dstArrayElement = 0;
    writes[0].dstBinding      = 0;
    
    vkUpdateDescriptorSets(data.device, 1, writes, 0, NULL);
    ASSERT_VK(vkEndCommandBuffer(data.command_buffer));
}

static void
init_pipeline()
{
    VkPipelineDynamicStateCreateInfo dynamic_state;
    VkDynamicState dynamic_state_enables[VK_DYNAMIC_STATE_RANGE_SIZE];
    VkPipelineVertexInputStateCreateInfo vi;
    VkPipelineInputAssemblyStateCreateInfo ia;
    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineColorBlendStateCreateInfo cb;
    VkPipelineColorBlendAttachmentState att_state[1];
    VkPipelineViewportStateCreateInfo vp;
    VkPipelineDepthStencilStateCreateInfo ds;
    VkPipelineMultisampleStateCreateInfo ms;
    VkGraphicsPipelineCreateInfo pipeline;
    
#if 1
    ASSERT_VK(vkBeginCommandBuffer(data.command_buffer, &data.cmd_buf_info));
#endif
    
    memset(dynamic_state_enables, 0x00, sizeof(dynamic_state_enables));
    
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.pNext             = NULL;
    dynamic_state.pDynamicStates    = dynamic_state_enables;
    dynamic_state.dynamicStateCount = 0;
    
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.pNext                           = NULL;
    vi.flags                           = 0;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &data.vi_binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions    = data.vi_attribs;
    
    ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.pNext                  = NULL;
    ia.flags                  = 0;
    ia.primitiveRestartEnable = VK_FALSE;
    ia.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    rs.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.pNext                   = NULL;
    rs.flags                   = 0;
    rs.polygonMode             = VK_POLYGON_MODE_FILL;
    rs.cullMode                = VK_CULL_MODE_BACK_BIT;
    rs.frontFace               = VK_FRONT_FACE_CLOCKWISE;
    rs.depthClampEnable        = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.depthBiasEnable         = VK_FALSE;
    rs.depthBiasConstantFactor = 0;
    rs.depthBiasClamp          = 0;
    rs.depthBiasSlopeFactor    = 0;
    rs.lineWidth               = 1.0f;
    
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.pNext = NULL;
    cb.flags = 0;
    
    att_state[0].colorWriteMask      = 0xF;
    att_state[0].blendEnable         = VK_FALSE;
    att_state[0].alphaBlendOp        = VK_BLEND_OP_ADD;
    att_state[0].colorBlendOp        = VK_BLEND_OP_ADD;
    att_state[0].srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    att_state[0].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    att_state[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    att_state[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    
    cb.attachmentCount   = 1;
    cb.pAttachments      = att_state;
    cb.logicOpEnable     = VK_FALSE;
    cb.logicOp           = VK_LOGIC_OP_NO_OP;
    cb.blendConstants[0] = 1.0f;
    cb.blendConstants[1] = 1.0f;
    cb.blendConstants[2] = 1.0f;
    cb.blendConstants[3] = 1.0f;
    
    vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.pNext         = NULL;
    vp.flags         = 0;
    vp.viewportCount = NUM_VIEWPORTS;
    vp.scissorCount  = NUM_SCISSORS;
    vp.pScissors     = NULL;
    vp.pViewports    = NULL;
    
    dynamic_state_enables[dynamic_state.dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT;
    dynamic_state_enables[dynamic_state.dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR;
    
    ds.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.pNext                 = NULL;
    ds.flags                 = 0;
    ds.depthTestEnable       = VK_TRUE;
    ds.depthWriteEnable      = VK_TRUE;
    ds.depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.minDepthBounds        = 0;
    ds.maxDepthBounds        = 0;
    ds.stencilTestEnable     = VK_FALSE;
    ds.back.failOp           = VK_STENCIL_OP_KEEP;
    ds.back.passOp           = VK_STENCIL_OP_KEEP;
    ds.back.compareOp        = VK_COMPARE_OP_ALWAYS;
    ds.back.compareMask      = 0;
    ds.back.reference        = 0;
    ds.back.depthFailOp      = VK_STENCIL_OP_KEEP;
    ds.back.writeMask        = 0;
    ds.front                 = ds.back;
    
    ms.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.pNext                 = NULL;
    ms.flags                 = 0;
    ms.pSampleMask           = NULL;
    ms.rasterizationSamples  = NUM_SAMPLES;
    ms.sampleShadingEnable   = VK_FALSE;
    ms.alphaToCoverageEnable = VK_FALSE;
    ms.alphaToOneEnable      = VK_FALSE;
    ms.minSampleShading      = 0.0;
    
    pipeline.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline.pNext               = NULL;
    pipeline.layout              = data.pipeline_layout;
    pipeline.basePipelineHandle  = VK_NULL_HANDLE;
    pipeline.basePipelineIndex   = 0;
    pipeline.flags               = 0;
    pipeline.pVertexInputState   = &vi;
    pipeline.pInputAssemblyState = &ia;
    pipeline.pRasterizationState = &rs;
    pipeline.pColorBlendState    = &cb;
    pipeline.pTessellationState  = NULL;
    pipeline.pMultisampleState   = &ms;
    pipeline.pDynamicState       = &dynamic_state;
    pipeline.pViewportState      = &vp;
    pipeline.pDepthStencilState  = &ds;
    pipeline.pStages             = data.shader_stages;
    pipeline.stageCount          = 2;
    pipeline.renderPass          = data.render_pass;
    pipeline.subpass             = 0;
    
    ASSERT_VK(vkCreateGraphicsPipelines(data.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &data.pipeline));
    ASSERT_VK(vkEndCommandBuffer(data.command_buffer));
} // End of init pipeline

static void
draw_cube()
{
    VkSemaphore  image_acquired_semaphore;
    VkSemaphoreCreateInfo  image_acquired_semaphore_create_info;
    VkRenderPassBeginInfo rp_begin;
    VkFenceCreateInfo fence_info;
    VkFence draw_fence;
    VkPipelineStageFlags pipe_stage_flags;
    VkPresentInfoKHR present;
    VkClearValue clear_values[2];
    const VkDeviceSize offsets[1] = { 0 };
    const VkCommandBuffer cmd_bufs[1] = { data.command_buffer };
    VkSubmitInfo submit_info[1];
    
    clear_values[0].color.float32[0]     = 0.0f;
    clear_values[0].color.float32[1]     = 0.0f;
    clear_values[0].color.float32[2]     = 0.0f;
    clear_values[0].color.float32[3]     = 0.0f;
    clear_values[1].depthStencil.depth   = 1.0f;
    clear_values[1].depthStencil.stencil = 0;
    
    image_acquired_semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    image_acquired_semaphore_create_info.pNext = NULL;
    image_acquired_semaphore_create_info.flags = 0;
    
    ASSERT_VK(vkCreateSemaphore(data.device, & image_acquired_semaphore_create_info, NULL, & image_acquired_semaphore));
    ASSERT_VK(vkAcquireNextImageKHR(data.device, data.swapchain, UINT64_MAX,  image_acquired_semaphore, VK_NULL_HANDLE, &data.current_buffer));
    
    rp_begin.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.pNext                    = NULL;
    rp_begin.renderPass               = data.render_pass;
    rp_begin.framebuffer              = data.framebuffers[data.current_buffer];
    rp_begin.renderArea.offset.x      = 0;
    rp_begin.renderArea.offset.y      = 0;
    rp_begin.renderArea.extent.width  = data.width;
    rp_begin.renderArea.extent.height = data.height;
    rp_begin.clearValueCount          = 2;
    rp_begin.pClearValues             = clear_values;
    
    ASSERT_VK(vkBeginCommandBuffer(data.command_buffer, &data.cmd_buf_info));
    
    vkCmdBeginRenderPass(data.command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(data.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, data.pipeline);
    vkCmdBindDescriptorSets(data.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, data.pipeline_layout, 0, NUM_DESCRIPTOR_SETS, data.descriptor_set, 0, NULL);
    vkCmdBindVertexBuffers(data.command_buffer, 0, 1, &data.vertex.buf, offsets);
    
    data.viewport.height   = (f32) data.height;
    data.viewport.width    = (f32) data.width;
    data.viewport.minDepth = 0.0f;
    data.viewport.maxDepth = 1.0f;
    data.viewport.x        = 0;
    data.viewport.y        = 0;
    
    data.scissor.extent.width  = data.width;
    data.scissor.extent.height = data.height;
    data.scissor.offset.x      = 0;
    data.scissor.offset.y      = 0;
    
    vkCmdSetViewport(data.command_buffer, 0, NUM_VIEWPORTS, &data.viewport);
    vkCmdSetScissor(data.command_buffer, 0, NUM_SCISSORS, &data.scissor);
    
    vkCmdDraw(data.command_buffer, 12 * 3, 1, 0, 0);
    vkCmdEndRenderPass(data.command_buffer);
    
    ASSERT_VK(vkEndCommandBuffer(data.command_buffer));
    
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.pNext = NULL;
    fence_info.flags = 0;
    
    vkCreateFence(data.device, &fence_info, NULL, &draw_fence);
    
    pipe_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    
    submit_info[0].pNext                = NULL;
    submit_info[0].sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info[0].waitSemaphoreCount   = 1;
    submit_info[0].pWaitSemaphores      = &image_acquired_semaphore; 
    submit_info[0].pWaitDstStageMask    = &pipe_stage_flags;
    submit_info[0].commandBufferCount   = 1;
    submit_info[0].pCommandBuffers      = cmd_bufs;
    submit_info[0].signalSemaphoreCount = 0;
    submit_info[0].pSignalSemaphores    = NULL;
    
    ASSERT_VK(vkQueueSubmit(data.graphics_queue, 1, submit_info, draw_fence));
    
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.pNext              = NULL;
    present.swapchainCount     = 1;
    present.pSwapchains        = &data.swapchain;
    present.pImageIndices      = &data.current_buffer;
    present.pWaitSemaphores    = NULL;
    present.waitSemaphoreCount = 0;
    present.pResults           = NULL;
    
    {
        VkResult res;
        do {
            res = vkWaitForFences(data.device, 1, &draw_fence, VK_TRUE, FENCE_TIMEOUT);
        } while (res == VK_TIMEOUT);
        ASSERT_VK(res);
    }
    
    ASSERT_VK(vkQueuePresentKHR(data.present_queue, &present));
    
    vkDestroySemaphore(data.device, image_acquired_semaphore, NULL);
    vkDestroyFence(data.device, draw_fence, NULL);
}

static void
destroy()
{
    vkDestroyPipeline(data.device, data.pipeline, NULL);
    
    vkDestroyDescriptorPool(data.device, data.descriptor_pool, NULL);
    
    vkDestroyBuffer(data.device, data.vertex.buf, NULL);
    vkFreeMemory(data.device, data.vertex.mem, NULL);
    
    for (u32 i = 0; i < data.swapchain_image_count; ++i) {
        vkDestroyFramebuffer(data.device, data.framebuffers[i], NULL);
    }
    
    vkDestroyShaderModule(data.device, data.shader_stages[0].module, NULL);
    vkDestroyShaderModule(data.device, data.shader_stages[1].module, NULL);
    
    vkDestroyRenderPass(data.device, data.render_pass, NULL);
    
    for (int i = 0; i < NUM_DESCRIPTOR_SETS; ++i) {
        vkDestroyDescriptorSetLayout(data.device, data.descriptor_layout[i], NULL);
    }
    
    vkDestroyPipelineLayout(data.device, data.pipeline_layout, NULL);
    
    vkDestroyBuffer(data.device, data.uniform.buf, NULL);
    vkFreeMemory(data.device, data.uniform.mem, NULL);
    
    vkDestroyImageView(data.device, data.depth.view, NULL);
    vkDestroyImage(data.device, data.depth.image, NULL);
    vkFreeMemory(data.device, data.depth.mem, NULL);
    
    for (u32 i = 0; i < data.swapchain_image_count; ++i) {
        vkDestroyImageView(data.device, data.buffers[i].view, NULL);
    }
    
    vkDestroySwapchainKHR(data.device, data.swapchain, NULL);
    
    vkFreeCommandBuffers(data.device, data.cbp, 1, &data.command_buffer);
    vkDestroyCommandPool(data.device, data.cbp, NULL);
    
    vkDeviceWaitIdle(data.device);
    vkDestroyDevice(data.device, NULL);
    
    vkDestroySurfaceKHR(data.instance, data.surface, NULL);
    xcb_destroy_window(data.connection, data.window);
    xcb_disconnect(data.connection);
    
    vkDestroyInstance(data.instance, NULL); 
}
