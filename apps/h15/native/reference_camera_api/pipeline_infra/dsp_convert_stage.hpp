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
    int m_output_height;                     /**< Height of the output data */
    int m_buffer_width = 256;                /**< Width of the output buffer pool */
    int m_buffer_height = 256;               /**< Height of the output buffer pool */
    bool m_save_frames = false;              /**< Flag to enable/disable frame saving to file */
    std::condition_variable m_available_buffers_cv;
    std::mutex m_buff_pool_mutex;
    StagePoolMode m_pool_mode = StagePoolMode::BLOCKING;
    std::mutex m_normalization; ///< Mutex for the active jobs counter.

    /**
     * @brief Constructor for DspConvertStage.
     * @param name The name of the stage.
     * @param buffer_width Width of the output buffer pool (default: 256).
     * @param buffer_height Height of the output buffer pool (default: 256).
     * @param queue_size Size of the queue for this stage.
     * @param leaky Indicates if the queue is leaky.
     * @param print_fps Flag to enable or disable printing FPS information.
     * @param save_frames Flag to enable or disable saving frames to file (default: false).
     */
    DspConvertStage(std::string name, int buffer_width = 256, int buffer_height = 256, size_t queue_size = 5, bool leaky = true, bool print_fps = false, bool save_frames = false) : ConnectedStage(name, queue_size, leaky, print_fps)
    {
        m_buffer_width = buffer_width;
        m_buffer_height = buffer_height;
        m_save_frames = save_frames;
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
     * @brief Initialize the DspConvertStage.
     * @return Status of the initialization.
     */
    AppStatus init() override
    {
        dsp_status device_status = create_device();
        if (device_status != DSP_SUCCESS)
        {
            return AppStatus::DSP_OPERATION_ERROR;
        }
        
        m_buffer_pool = std::make_shared<MediaLibraryBufferPool>(m_buffer_width, m_buffer_height, HAILO_FORMAT_RGB,
                                                                 m_output_pool_size, HAILO_MEMORY_TYPE_DMABUF);

        if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
        {
            return AppStatus::BUFFER_ALLOCATION_ERROR;
        }
        return AppStatus::SUCCESS;
    }

    /**
     * @brief Deinitialize the DspConvertStage.
     * @return Status of the deinitialization.
     */
    AppStatus deinit() override
    {
        return AppStatus::SUCCESS;
    }

    /**
     * @brief Save a frame to a file.
     * @param buffer The buffer containing the frame data.
     * @param width Width of the frame.
     * @param height Height of the frame.
     * @param filename Path to the output file.
     * @return Status of the save operation.
     */
    AppStatus save_frame_to_file(const HailoMediaLibraryBufferPtr& buffer, int width, int height, const std::string& filename)
    {
        if (!buffer || !buffer->buffer_data)
        {
            std::cerr << "Error: Invalid buffer or buffer_data" << std::endl;
            return AppStatus::BUFFER_ALLOCATION_ERROR;
        }

        try
        {
            std::ofstream file(filename, std::ios::binary);
            if (!file.is_open())
            {
                std::cerr << "Failed to open file: " << filename << std::endl;
                return AppStatus::BUFFER_ALLOCATION_ERROR;
            }

            // Calculate the size of RGB buffer (3 bytes per pixel)
            size_t buffer_size = width * height * 3;
            
            // Get pointer to the buffer data using get_plane_ptr
            uint8_t* data_ptr = (uint8_t *)buffer->get_plane_ptr(0);
            
            if (!data_ptr)
            {
                std::cerr << "Error: Failed to get plane pointer from buffer" << std::endl;
                file.close();
                return AppStatus::BUFFER_ALLOCATION_ERROR;
            }
            
            file.write(reinterpret_cast<const char*>(data_ptr), buffer_size);
            file.flush();
            file.close();

            std::cout << "Frame saved successfully to: " << filename << " (size: " << buffer_size << " bytes)" << std::endl;
            return AppStatus::SUCCESS;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Exception while saving frame: " << e.what() << std::endl;
            return AppStatus::BUFFER_ALLOCATION_ERROR;
        }
    }

    /**
     * @brief Process the given data buffer and convert from NV12 to RGB.
     * @param data The data buffer to process.
     * @return Status of the processing.
     */
    AppStatus process(BufferPtr data) override
    {
        dsp_image_properties_t *src;
        src = new dsp_image_properties_t();

        dsp_image_properties_t *output;
        output = new dsp_image_properties_t();

        auto status1 = dsp_utils::hailo_buffer_data_to_dsp_image_props(data->get_buffer()->buffer_data.get(), src);
        if (status1 != DSP_SUCCESS)
        {
            delete src;
            delete output;
            return AppStatus::BUFFER_ALLOCATION_ERROR;
        }

        HailoMediaLibraryBufferPtr rgb_image_buffer = std::make_shared<hailo_media_library_buffer>();
        BufferPtr rgb_image_buffer_ptr = std::make_shared<Buffer>(rgb_image_buffer);
        
        if (m_buffer_pool->get_available_buffers_count() > 0)
        {
            if (m_buffer_pool->acquire_buffer(rgb_image_buffer) != MEDIA_LIBRARY_SUCCESS)
            {
                rgb_image_buffer.reset();
                delete src;
                delete output;
                return AppStatus::SUCCESS;
            }
        }
        else
        {
            rgb_image_buffer.reset();
            delete src;
            delete output;
            return AppStatus::SUCCESS;
        }
        
        auto status = dsp_utils::hailo_buffer_data_to_dsp_image_props(rgb_image_buffer->buffer_data.get(), output);
        if (status != DSP_SUCCESS)
        {
            delete src;
            delete output;
            return AppStatus::BUFFER_ALLOCATION_ERROR;
        }

        status = dsp_convert_format(device, src, output);
        if (status != DSP_SUCCESS)
        {
            delete src;
            delete output;
            return AppStatus::DSP_OPERATION_ERROR;
        }

        // Save the converted frame to file if enabled (save every 5th frame to reduce I/O)
        if (m_save_frames)
        {
            static int frame_count = 0;
            if (frame_count % 5 == 0)
            {
                std::string output_filename = "/tmp/frame_" + std::to_string(frame_count) + ".raw";
                std::cout << "Attempting to save frame " << frame_count << " to " << output_filename << std::endl;
                AppStatus save_status = save_frame_to_file(rgb_image_buffer, m_buffer_width, m_buffer_height, output_filename);
                if (save_status != AppStatus::SUCCESS)
                {
                    std::cerr << "Warning: Failed to save frame to file: " << output_filename << std::endl;
                }
            }
            frame_count++;
        }

        send_to_subscribers(rgb_image_buffer_ptr);
        delete src;
        delete output;
        return AppStatus::SUCCESS;
    }
};
