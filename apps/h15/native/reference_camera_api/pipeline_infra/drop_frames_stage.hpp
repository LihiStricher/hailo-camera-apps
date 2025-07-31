#pragma once

// General includes
#include <algorithm>
#include <iostream>

// Media-Library includes
#include "media_library/encoder.hpp"

// Tappas includes
#include "hailo_common.hpp"

// Infra includes
#include "stage.hpp"
#include "buffer.hpp"


class DropFrameStage : public ConnectedStage
{
private:
    inline static int m_frame; ///< Static frame counter to track the number of frames processed. 
    int fd_fps = 0; ///< Fire detection FPS, used to control the frame dropping rate.
public:
    DropFrameStage(std::string name, size_t queue_size = 1, bool leaky = false, bool print_fps = false, int fd_fps = 3) : ConnectedStage(name, queue_size, leaky, print_fps)
    {
        this->fd_fps = fd_fps; // Set the fire detection FPS
    }


    AppStatus init() override
    {
        m_frame = 0; // Initialize the frame counter to zero
        return AppStatus::SUCCESS;
    }

    AppStatus deinit() override
    {

        return AppStatus::SUCCESS;
    }

    AppStatus process(BufferPtr data)
    {   
        if(fd_fps <= 0)
        {
            return AppStatus::SUCCESS;
        }

        m_frame++;
        if(m_frame % (15/fd_fps) != 0)
        {
            return AppStatus::SUCCESS; // Skip processing this frame
        }

        data->add_time_stamp(m_stage_name);
        set_duration(data);
        send_to_subscribers(data);

        return AppStatus::SUCCESS;
    }
};
