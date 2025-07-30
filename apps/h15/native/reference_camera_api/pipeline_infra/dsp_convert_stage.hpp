#pragma once
// General includes
#include <atomic>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

// HailoRT includes
#include "hailo/hailort.hpp"
#include "media_library/dsp_utils.hpp"

// Tappas includes
#include "hailo_objects.hpp"

// Media library includes
#include "media_library/media_library_types.hpp"

// Infra includes
#include "buffer.hpp"
#include "queue.hpp"
#include "stage.hpp"

class DspConvertStage : public ConnectedStage
{
public:
    dsp_device device = NULL;                /**< Hailo DSP device handle */
    MediaLibraryBufferPoolPtr m_buffer_pool; /**< Buffer pool for managing media library buffers */
    int m_output_pool_size = 10;             /**< Size of the output buffer pool */
    int m_input_width;                       /**< Width of the input data */
    int m_input_height;                      /**< Height of the input data */
    int m_output_width;                      /**< Width of the output data */
    int m_output_hight;                      /**< Height of the output data */
    std::condition_variable m_available_buffers_cv;
    std::mutex m_buff_pool_mutex;
    StagePoolMode m_pool_mode = StagePoolMode::BLOCKING;
    std::mutex m_normalization; ///< Mutex for the active jobs counter.

    /**
     * @brief Constructor for OverlayStage.
     * @param name The name of the stage.
     * @param queue_size Size of the queue for this stage.
     * @param leaky Indicates if the queue is leaky.
     * @param print_fps Flag to enable or disable printing FPS information.
     */
    DspConvertStage(std::string name, size_t queue_size = 5, bool leaky = true, bool print_fps = false) : ConnectedStage(name, queue_size, leaky, print_fps)
    {
    }

    dsp_status create_device()
    {
        dsp_status create_device_status = DSP_UNINITIALIZED;
        // If the device is not initialized, initialize it, else return SUCCESS
        if (device == NULL)
        {
            create_device_status = dsp_create_device(&device);
            if (create_device_status != DSP_SUCCESS)
            {
                std::cout << "failed to create device" << std::endl;
                return create_device_status;
            }
        }

        return DSP_SUCCESS;
    }

    dsp_status release_device()
    {
        if (device == NULL)
        {
            return DSP_SUCCESS;
        }

        return DSP_SUCCESS;
    }

    /**
     * @brief Initialize the overlay stage.
     * @return Status of the initialization.
     */
    AppStatus init() override
    {
        create_device();
        m_buffer_pool = std::make_shared<MediaLibraryBufferPool>(256, 256, HAILO_FORMAT_RGB,
                                                                 m_output_pool_size, HAILO_MEMORY_TYPE_DMABUF);

        if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
        {
            std::cout << "Failed to initialize buffer pool" << std::endl;
            return AppStatus::BUFFER_ALLOCATION_ERROR;
        }
        return AppStatus::SUCCESS;
    }

    /**
     * @brief Deinitialize the overlay stage.
     * @return Status of the deinitialization.
     */
    AppStatus deinit() override
    {
        return AppStatus::SUCCESS;
    }

    /**
     * @brief Process the given data buffer and apply overlay.
     * @param data The data buffer to process.
     * @return Status of the processing.
     */
    AppStatus process(BufferPtr data)
    {
        dsp_image_properties_t *src;
        src = new dsp_image_properties_t();

        dsp_image_properties_t *output;
        output = new dsp_image_properties_t();

        auto status1 = dsp_utils::hailo_buffer_data_to_dsp_image_props(data->get_buffer()->buffer_data.get(), src);
        if (status1 != DSP_SUCCESS)
        {
            std::cout << "Failed to convert from buffer data to dsp image props" << std::endl;
            return AppStatus::BUFFER_ALLOCATION_ERROR;
        }

        HailoMediaLibraryBufferPtr rgb_image_buffer = std::make_shared<hailo_media_library_buffer>();
        BufferPtr rgb_image_buffer_ptr = std::make_shared<Buffer>(rgb_image_buffer);
        if (m_buffer_pool->get_available_buffers_count() > 0)
        {
            if (m_buffer_pool->acquire_buffer(rgb_image_buffer) != MEDIA_LIBRARY_SUCCESS)
            {
                rgb_image_buffer.reset();
                return AppStatus::SUCCESS;
            }
        }
        else
        {
            rgb_image_buffer.reset();
            return AppStatus::SUCCESS;
        }
        auto status = dsp_utils::hailo_buffer_data_to_dsp_image_props(rgb_image_buffer->buffer_data.get(), output);
        if (status != DSP_SUCCESS)
        {
            std::cout << "Failed to conver from buffer data to dsp image probs" << std::endl;
        }

        status = dsp_convert_format(device, src, output);
        if (status != DSP_SUCCESS)
        {
            std::cout << "Failed to convert the format" << std::endl;
        }
        static std::atomic<int> file_index{0};
        auto current_index = file_index.fetch_add(1);

        if (current_index >= 6)
        {
            file_index.store(0);
            current_index = 0;
        }

        send_to_subscribers(rgb_image_buffer_ptr);
        delete src;
        delete output;
        return AppStatus::SUCCESS;
    }
};
