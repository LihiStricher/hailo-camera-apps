#pragma once

// Media-Library includes
#include "media_library/buffer_pool.hpp"
#include "media_library/dma_memory_allocator.hpp"

// Tappas includes
#include "hailo_common.hpp"

// Infra includes
#include "stage.hpp"
#include "buffer.hpp"

class FileSourceStage : public ConnectedStage
{
  private:
    std::string m_file_location;
    size_t m_width;
    size_t m_height;
    size_t m_frame_size;
    size_t m_total_frames;
    size_t m_current_frame_index;
    bool m_loop_enabled;
    double m_fps;
    std::chrono::milliseconds m_frame_interval;
    size_t m_buffer_pool_size;

    std::ifstream m_file_stream;
    std::shared_ptr<MediaLibraryBufferPool> m_buffer_pool;

  public:
    FileSourceStage(std::string name, size_t queue_size, bool leaky, bool print_fps, size_t buffer_pool_size)
        : ConnectedStage(name, queue_size, leaky, print_fps), m_width(0), m_height(0), m_frame_size(0),
          m_total_frames(0), m_current_frame_index(0), m_loop_enabled(true), m_fps(0.0), m_frame_interval(33),
          m_buffer_pool_size(buffer_pool_size)
    {
    }

    /**
     * @brief Configure the file source with file location and video parameters
     * @param file_location Path to the raw video file (NV12 format)
     * @param width Video width in pixels
     * @param height Video height in pixels
     * @param fps Frames per second for playback
     * @param loop_enabled Whether to loop the video when it reaches the end
     * @return AppStatus indicating success or failure
     */
    AppStatus configure(const std::string &file_location, size_t width, size_t height, double fps, bool loop_enabled);

    /**
     * @brief Initialize the file source stage
     * @return AppStatus indicating success or failure
     */
    AppStatus init() override;

    /**
     * @brief Deinitialize the file source stage
     * @return AppStatus indicating success or failure
     */
    AppStatus deinit() override;

    /**
     * @brief Stop the file source stage
     * @return AppStatus indicating success or failure
     */
    AppStatus stop() override;

    /**
     * @brief Main loop that reads frames from file and sends them to subscribers
     */
    void loop() override;

  private:
    /**
     * @brief Create and initialize the buffer pool
     * @return AppStatus indicating success or failure
     */
    AppStatus create_buffer_pool();

    /**
     * @brief Read the next frame from the file into a buffer
     * @param buffer Buffer to read the frame into
     * @return True if frame was read successfully, false otherwise
     */
    bool read_next_frame(HailoMediaLibraryBufferPtr buffer);

    /**
     * @brief Calculate frame size based on width and height (assuming NV12 format)
     * @return Frame size in bytes
     */
    size_t calculate_frame_size() const;

    /**
     * @brief Validate that the file exists and has the expected size
     * @return True if file is valid, false otherwise
     */
    bool validate_file();
};

class FileSourceStageBuild : public FileSourceStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_file_location;
        std::optional<size_t> m_width;
        std::optional<size_t> m_height;
        std::optional<double> m_fps;
        size_t m_queue_size = 10;
        bool m_leaky = false;
        bool m_print_fps = false;
        size_t m_buffer_pool_size = 20;

      public:
        Builder &set_stage_name(std::string name)
        {
            m_stage_name = name;
            return *this;
        }

        Builder &set_file_location(std::string file_location)
        {
            m_file_location = file_location;
            return *this;
        }

        Builder &set_width(size_t width)
        {
            m_width = width;
            return *this;
        }

        Builder &set_height(size_t height)
        {
            m_height = height;
            return *this;
        }

        Builder &set_fps(double fps)
        {
            m_fps = fps;
            return *this;
        }

        Builder &set_queue_size_opt(size_t size)
        {
            m_queue_size = size;
            return *this;
        }

        Builder &set_leaky_opt(bool activate)
        {
            m_leaky = activate;
            return *this;
        }

        Builder &set_printfps_opt(bool activate)
        {
            m_print_fps = activate;
            return *this;
        }

        Builder &set_buffer_pool_size_opt(size_t size)
        {
            m_buffer_pool_size = size;
            return *this;
        }

        std::shared_ptr<FileSourceStage> buildptr() const
        {
            auto stage = std::make_shared<FileSourceStage>(m_stage_name.value(), m_queue_size, m_leaky, m_print_fps,
                                                           m_buffer_pool_size);
            stage->configure(m_file_location.value(), m_width.value(), m_height.value(), m_fps.value(), true);
            return stage;
        }
    };

    static Builder create()
    {
        return Builder();
    }
};
