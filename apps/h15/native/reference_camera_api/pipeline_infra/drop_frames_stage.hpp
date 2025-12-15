#pragma once

// General includes
#include <algorithm>
#include <iostream>
#include <chrono>

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
    int m_frame; ///< Static frame counter to track the number of frames processed. 
    int fps = 0; ///< Target FPS, used to control the frame dropping rate.
    int original_fps = 30; ///< Original input FPS.
    
public:
    DropFrameStage(std::string name, size_t queue_size = 1, bool leaky = false, bool print_fps = false, int fps = 3, int original_fps = 30) : ConnectedStage(name, queue_size, leaky, print_fps)
    {
        this->fps = fps; // Set the target FPS
        this->original_fps = original_fps; // Set the original input FPS
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

    AppStatus process(BufferPtr data) override
    {
        if(fps <= 0)
        {
            return AppStatus::SUCCESS;
        }

        // if(m_stage_name == "ai_pipeline_drop_frames_stage"){
        //     m_frame++;
        // }
        this->m_frame++;
        if(this->m_frame % (original_fps/fps) != 0)
        {
            return AppStatus::SUCCESS; // Skip processing this frame
        }

        data->add_time_stamp(m_stage_name);
        set_duration(data);
        send_to_subscribers(data);

        return AppStatus::SUCCESS;
    }
};
