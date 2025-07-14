#pragma once

#include "stage.hpp"
#include "buffer.hpp"
#include "hailo_objects.hpp"
#include "hailo_common.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <optional>
#include <iostream>
#define QUEUE_SIZE_DEFAULT (1)

class ScaleBboxStage : public ConnectedStage
{
  private:
    float m_scale;
    int m_class_id;

  public:
    explicit ScaleBboxStage(std::string name, size_t queue_size = QUEUE_SIZE_DEFAULT, bool leaky = false,
                            float m_scale = 1.4f, int class_id = -1, bool print_fps = false)
        : ConnectedStage(name, queue_size, leaky, print_fps), m_scale(m_scale), m_class_id(class_id)
    {
    }

    AppStatus init() override
    {
        return AppStatus::SUCCESS;
    }

    AppStatus deinit() override
    {
        return AppStatus::SUCCESS;
    }

    AppStatus process(BufferPtr input_buffer) override
    {
        HailoROIPtr roi = input_buffer->get_roi();

        HailoBBox roi_bbox = hailo_common::create_flattened_bbox(roi->get_bbox(), roi->get_scaling_bbox());
        auto detections = hailo_common::get_hailo_detections(roi);

        for (auto detection : detections)
        {
            if (m_class_id == -1 || detection->get_class_id() == m_class_id)
            {

                auto bbox = detection->get_bbox();
                float cx = bbox.xmin() + bbox.width() / 2.0f;
                float cy = bbox.ymin() + bbox.height() / 2.0f;
                float new_w = std::min(bbox.width() * m_scale, 1.0f);
                float new_h = std::min(bbox.height() * m_scale, 1.0f);
                float new_xmin = std::clamp(cx - new_w * 0.5f, 0.0f, 1.0f - new_w);
                float new_ymin = std::clamp(cy - new_h * 0.5f, 0.0f, 1.0f - new_h);

                HailoBBox enlarged_bbox(new_xmin * roi_bbox.width() + roi_bbox.xmin(),
                                        new_ymin * roi_bbox.height() + roi_bbox.ymin(), new_w * roi_bbox.width(),
                                        new_h * roi_bbox.height());

                detection->set_bbox(enlarged_bbox);
            }
        }

        roi->clear_scaling_bbox();

        // Push the buffer to the next stage
        input_buffer->add_time_stamp(m_stage_name);
        set_duration(input_buffer);
        send_to_subscribers(input_buffer);
        return AppStatus::SUCCESS;
    }
};

class ScaleBboxStageBuild : public ScaleBboxStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = 1;
        bool m_leaky = false;
        float m_scale = 1.4f;
        int m_class_id = -1;
        bool m_print_fps = false;

      public:
        Builder &set_stage_name(const std::string &name)
        {
            m_stage_name = name;
            return *this;
        }
        Builder &set_scale(float scale)
        {
            m_scale = scale;
            return *this;
        }
        Builder &set_class_id(int class_id)
        {
            m_class_id = class_id;
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

        std::shared_ptr<ScaleBboxStage> buildptr() const
        {
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

            return std::make_shared<ScaleBboxStage>(m_stage_name.value(), m_queue_size, m_leaky, m_scale, m_class_id,
                                                    m_print_fps);
        }
    };

    static Builder create()
    {
        return Builder();
    }
};
