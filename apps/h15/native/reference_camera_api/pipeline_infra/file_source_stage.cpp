#include "file_source_stage.hpp"

#include <chrono>
#include <thread>
#include <fstream>
#include <string>
#include <memory>

AppStatus FileSourceStage::configure(const std::string &file_location, size_t width, size_t height, double fps,
                                     bool loop_enabled)
{
    m_file_location = file_location;
    m_width = width;
    m_height = height;
    m_fps = fps;
    m_loop_enabled = loop_enabled;
    m_current_frame_index = 0;

    m_frame_size = calculate_frame_size();

    m_frame_interval = std::chrono::milliseconds(static_cast<long>(1000.0 / fps));

    if (!validate_file())
    {
        REFERENCE_CAMERA_LOG_ERROR("File validation failed for: {}", file_location);
        return AppStatus::CONFIGURATION_ERROR;
    }

    REFERENCE_CAMERA_LOG_INFO("Configured FileSourceStage with file: {}, resolution: {}x{}, fps: {}, frames: {}",
                              file_location, width, height, fps, m_total_frames);

    return AppStatus::SUCCESS;
}

AppStatus FileSourceStage::init()
{
    if (m_file_location.empty())
    {
        REFERENCE_CAMERA_LOG_ERROR("File location not configured. Call configure() first.");
        return AppStatus::UNINITIALIZED;
    }

    AppStatus status = create_buffer_pool();
    if (status != AppStatus::SUCCESS)
    {
        REFERENCE_CAMERA_LOG_ERROR("Failed to create buffer pool");
        return status;
    }

    m_file_stream.open(m_file_location, std::ios::in | std::ios::binary);
    if (!m_file_stream.is_open())
    {
        REFERENCE_CAMERA_LOG_ERROR("Failed to open file: {}", m_file_location);
        return AppStatus::CONFIGURATION_ERROR;
    }

    REFERENCE_CAMERA_LOG_INFO("FileSourceStage initialized successfully");
    return AppStatus::SUCCESS;
}

AppStatus FileSourceStage::deinit()
{
    if (m_file_stream.is_open())
    {
        m_file_stream.close();
    }

    REFERENCE_CAMERA_LOG_INFO("FileSourceStage deinitialized");
    return AppStatus::SUCCESS;
}

AppStatus FileSourceStage::stop()
{
    set_end_of_stream(true);
    m_thread.join();
    return AppStatus::SUCCESS;
}

void FileSourceStage::loop()
{
    REFERENCE_CAMERA_LOG_INFO("FileSourceStage loop started");

    if (init() != AppStatus::SUCCESS)
    {
        REFERENCE_CAMERA_LOG_ERROR("Failed to initialize FileSourceStage");
        return;
    }

    auto last_frame_time = std::chrono::steady_clock::now();

    while (!m_end_of_stream)
    {
        HailoMediaLibraryBufferPtr buffer = std::make_shared<hailo_media_library_buffer>();
        if (m_buffer_pool->acquire_buffer(buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            REFERENCE_CAMERA_LOG_WARN("Failed to acquire buffer, skipping frame");
            continue;
        }

        if (!read_next_frame(buffer))
        {
            REFERENCE_CAMERA_LOG_INFO("Stopping.");
            set_end_of_stream(true);
            break;
        }

        // Calculate sleep time after frame is ready but before sending
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_frame_time);

        if (elapsed < m_frame_interval)
        {
            std::this_thread::sleep_for(m_frame_interval - elapsed);
        }

        BufferPtr wrapped_buffer = std::make_shared<Buffer>(buffer);
        wrapped_buffer->get_buffer()->isp_timestamp_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();

        send_to_subscribers(wrapped_buffer);

        last_frame_time = std::chrono::steady_clock::now();
    }

    deinit();
    REFERENCE_CAMERA_LOG_INFO("FileSourceStage loop ended");
}

AppStatus FileSourceStage::create_buffer_pool()
{
    const std::string pool_name = m_stage_name + "_buffer_pool";

    m_buffer_pool = std::make_shared<MediaLibraryBufferPool>(m_width, m_height, HAILO_FORMAT_NV12, m_buffer_pool_size,
                                                             HAILO_MEMORY_TYPE_DMABUF, pool_name);

    if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        REFERENCE_CAMERA_LOG_ERROR("Failed to initialize buffer pool");
        return AppStatus::BUFFER_ALLOCATION_ERROR;
    }

    REFERENCE_CAMERA_LOG_INFO("Created buffer pool: {} with {} buffers of size {}x{}", pool_name, m_buffer_pool_size,
                              m_width, m_height);

    return AppStatus::SUCCESS;
}

bool FileSourceStage::read_next_frame(HailoMediaLibraryBufferPtr buffer)
{
    if (!m_file_stream.is_open())
    {
        REFERENCE_CAMERA_LOG_ERROR("File stream is not open");
        return false;
    }

    // Handle end-of-file and loopback logic upfront
    if (m_current_frame_index >= m_total_frames)
    {
        if (m_loop_enabled)
        {
            REFERENCE_CAMERA_LOG_DEBUG("Looping back to beginning of file");
            m_file_stream.clear(); // Clear EOF flag
            m_current_frame_index = 0;
        }
        else
        {
            REFERENCE_CAMERA_LOG_INFO("Reached end of file");
            return false;
        }
    }

    m_file_stream.seekg(m_current_frame_index * m_frame_size, std::ios::beg);

    auto read_plane = [&](int plane_index, size_t size, const char *plane_name) -> bool {
        DmaMemoryAllocator::get_instance().dmabuf_sync_start(buffer->get_plane_ptr(plane_index));
        m_file_stream.read(reinterpret_cast<char *>(buffer->get_plane_ptr(plane_index)), size);
        DmaMemoryAllocator::get_instance().dmabuf_sync_end(buffer->get_plane_ptr(plane_index));

        if (!m_file_stream)
        {
            REFERENCE_CAMERA_LOG_ERROR("Failed to read {} plane", plane_name);
            return false;
        }

        return true;
    };

    size_t y_size = m_width * m_height;
    if (!read_plane(0, y_size, "Y"))
    {
        return false;
    }

    size_t uv_size = m_width * m_height / 2;
    if (!read_plane(1, uv_size, "UV"))
    {
        return false;
    }

    m_current_frame_index++;

    return true;
}

size_t FileSourceStage::calculate_frame_size() const
{
    return m_width * m_height * 3 / 2;
}

bool FileSourceStage::validate_file()
{
    std::ifstream test_stream(m_file_location, std::ios::in | std::ios::binary | std::ios::ate);
    if (!test_stream.is_open())
    {
        REFERENCE_CAMERA_LOG_ERROR("Cannot open file: {}", m_file_location);
        return false;
    }

    // Get file size
    auto file_size = test_stream.tellg();
    test_stream.close();

    if (file_size <= 0)
    {
        REFERENCE_CAMERA_LOG_ERROR("File is empty: {}", m_file_location);
        return false;
    }

    size_t expected_frame_size = calculate_frame_size();

    if (file_size % expected_frame_size != 0)
    {
        REFERENCE_CAMERA_LOG_ERROR("File size ({}) is not a multiple of frame size ({}). "
                                   "File may be corrupted or have incorrect resolution.",
                                   static_cast<size_t>(file_size), expected_frame_size);
        return false;
    }

    m_total_frames = file_size / expected_frame_size;

    if (m_total_frames == 0)
    {
        REFERENCE_CAMERA_LOG_ERROR("Calculated zero frames in file");
        return false;
    }

    REFERENCE_CAMERA_LOG_INFO("File validation successful: {} frames of size {} bytes each", m_total_frames,
                              expected_frame_size);

    return true;
}
