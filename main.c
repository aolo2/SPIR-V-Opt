#include "common.h"
#include "linmath.h"

#include <vulkan/vulkan.h>
#include <xcb/xcb.h>
#include <sys/inotify.h>
#include <pthread.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

#define NUM_SAMPLES VK_SAMPLE_COUNT_1_BIT
#define NUM_DESCRIPTOR_SETS 1
#define NUM_SHADER_STAGES 2
#define NUM_VERT_ATTRIBUTES 2
#define NUM_VIEWPORTS 1
#define NUM_SCISSORS NUM_VIEWPORTS
#define FENCE_TIMEOUT 100000000

struct swapchain_buffer {
    VkImage     image;
    VkImageView view;
};

struct depth_buffer {
    VkFormat       format;
    VkImage        image;
    VkDeviceMemory mem;
    VkImageView    view;
};

struct uniform_buffer {
    VkBuffer               buf;
    VkDeviceMemory         mem;
    VkDescriptorBufferInfo buffer_info;
};

struct vertex_buffer {
    VkBuffer               buf;
    VkDeviceMemory         mem;
    VkDescriptorBufferInfo buffer_info;
};

static struct {
    VkInstance                        instance;
    VkPhysicalDevice                 *gpus;
    VkQueueFamilyProperties          *queue_properties;
    VkDeviceCreateInfo                device_info;
    VkDevice                          device;
    VkCommandPool                     cbp;
    VkCommandBuffer                   command_buffer;
    VkSurfaceKHR                      surface;
    VkFormat                          format;
    VkSwapchainKHR                    swapchain;
    VkImage                          *swapchain_images;
    struct swapchain_buffer          *buffers;
    struct depth_buffer               depth;
    struct uniform_buffer             uniform;
    struct vertex_buffer              vertex;
    VkPhysicalDeviceProperties        gpu_props;
    VkPhysicalDeviceMemoryProperties  memory_properties;
    VkDescriptorSetLayout             descriptor_layout[NUM_DESCRIPTOR_SETS];
    VkPipelineLayout                  pipeline_layout;
    VkDescriptorPool                  descriptor_pool;
    VkDescriptorSet                   descriptor_set[NUM_DESCRIPTOR_SETS];
    VkRenderPass                      render_pass;
    VkPipelineShaderStageCreateInfo   shader_stages[NUM_SHADER_STAGES];
    VkFramebuffer                    *framebuffers;
    VkVertexInputBindingDescription   vi_binding;
    VkVertexInputAttributeDescription vi_attribs[NUM_VERT_ATTRIBUTES];
    VkPipeline                        pipeline;
    VkViewport                        viewport;
    VkRect2D                          scissor;
    VkQueue                           graphics_queue;
    VkQueue                           present_queue;
    VkCommandBufferBeginInfo          cmd_buf_info;
    VkMemoryRequirements              umem_reqs;
    
    xcb_connection_t           *connection;
    xcb_screen_t               *screen;
    xcb_window_t                window;
    xcb_intern_atom_reply_t    *atom_wm_delete_window;
    
    mat4x4 projection;
    mat4x4 view;
    mat4x4 model;
    mat4x4 clip;
    mat4x4 mvp;
    
    u8 *ubuffer_data;
    u32 current_buffer;
    u32 swapchain_image_count;
    u32 graphics_queue_family_index;
    u32 present_queue_family_index;
    u32 queue_family_count;
    u32 gpu_count;
    u32 width;
    u32 height;
    const char **device_extension_names;
} data;

static const u32 TARGET_FRAMERATE = 60;
static const f32 TARGET_FRAMETIME = 1000.0f / (f32) TARGET_FRAMERATE;

static bool shader_update = false;
static pthread_mutex_t su_mutex = PTHREAD_MUTEX_INITIALIZER;

#include "data/cube.h"
#include "vk_utils.h"

static void *
in_worker(void *arg)
{
    char buffer[EVENT_BUF_LEN];
    s32 fd = inotify_init();
    char *p;
    struct inotify_event *event;
    s32 len;
    
    inotify_add_watch(fd, "shaders/sample.frag.spv", IN_MODIFY);
    
    while (true) {
        
        len = read(fd, buffer, EVENT_BUF_LEN); 
        p = buffer;
        
        while (p < buffer + len) {
            event = (struct inotify_event *) p;
            printf("event\n");
            
            if (event->mask & IN_MODIFY) {
                //pthread_mutex_lock(&su_mutex);
                printf("reloading shader\n");
                shader_update = true;
                //pthread_mutex_unlock(&su_mutex);
            }
            
            p += sizeof(struct inotify_event) + event->len;
        }
    }
    
    return(NULL);
}

s32
main(void)
{
    init_instance();
    enumerate_devices();
    init_surface();
    init_device();
    init_command_buffer();
    init_swapchain();
    init_depth_buffer();
    init_uniform_buffer();
    init_pipeline_layout();
    init_render_pass();
    init_shaders();
    init_framebuffers();
    init_vertex_buffer();
    init_descriptor_poolset();
    init_pipeline();
    
    struct timespec frametime_beg;
    struct timespec frametime_end;
    
    u32 fn = 0;
    
    pthread_t in_worker_thread;
    pthread_create(&in_worker_thread, NULL, in_worker, NULL);
    
    while (true) {
        clock_gettime(CLOCK_MONOTONIC, &frametime_beg);
        
        mat4x4_identity(data.model);
        mat4x4_rotate_Y(data.model, data.model, (f32) fn / 100);
        mat4x4_mul(data.mvp, data.clip, data.projection);
        mat4x4_mul(data.mvp, data.mvp, data.view);
        mat4x4_mul(data.mvp, data.mvp, data.model);
        
        if (shader_update) {
            //pthread_mutex_lock(&su_mutex);
            
            rebuild_fragment_shader();
            init_pipeline();
            shader_update = false;
            
            //pthread_mutex_unlock(&su_mutex);
        }
        
        update_uniform_data(data.umem_reqs.size);
        draw_cube();
        
        clock_gettime(CLOCK_MONOTONIC, &frametime_end);
        
        s64 frametime_nsec = (frametime_end.tv_nsec + frametime_end.tv_sec * 1000000000) 
            - (frametime_beg.tv_nsec + frametime_beg.tv_sec * 1000000000);
        
        s64 frametime_msec = frametime_nsec / 1000000;
        s32 frametime_delta = TARGET_FRAMETIME - frametime_msec;
        
        if (frametime_delta > 0) {
            usleep(frametime_delta * 1000);
        }
        
        ++fn;
    }
    
    pthread_join(in_worker_thread, NULL);
    
    destroy();
    
    return(0);
}