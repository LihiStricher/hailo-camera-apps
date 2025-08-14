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

#include "hailo_objects.hpp"
#include "zmq.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

class OutputMetadataStage : public ConnectedStage
{
private:
    zmq::context_t zmq_context;
    zmq::socket_t zmq_publisher;
    bool init_done = false;

public:
    OutputMetadataStage(std::string name, size_t queue_size = 1, bool leaky = false, bool print_fps = false) : ConnectedStage(name, queue_size, leaky, print_fps)
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

    bool is_intrusion(
        float obj_xmin, float obj_ymin, float obj_xmax, float obj_ymax,
        float zone_xmin, float zone_ymin, float zone_xmax, float zone_ymax)
    {
        float overlap_xmin = std::max(obj_xmin, zone_xmin);
        float overlap_ymin = std::max(obj_ymin, zone_ymin);
        float overlap_xmax = std::min(obj_xmax, zone_xmax);
        float overlap_ymax = std::min(obj_ymax, zone_ymax);

        return (overlap_xmin < overlap_xmax) && (overlap_ymin < overlap_ymax);
    }

    /**
     * @brief Initialize the ZeroMQ PUB socket for publishing messages.
     *
     * @param address The address to bind the PUB socket to.
     */
    void init_zmq_publisher(const std::string &address)
    {
        try
        {
            zmq_publisher = zmq::socket_t(zmq_context, zmq::socket_type::pub);
            zmq_publisher.bind(address);
            std::cout << "ZMQ publisher initialized at " << address << std::endl;
        }
        catch (const zmq::error_t &e)
        {
            std::cerr << "Error initializing ZMQ publisher: " << e.what() << std::endl;
        }
    }

    AppStatus process(BufferPtr data)
    {
        if (!init_done)
        {
            init_zmq_publisher("tcp://10.0.0.1:7000");
            init_done = true;
        }

        auto roi = data->get_roi();
        json all_messages = json::array(); // accumulate all object messages here

        for (auto obj : roi->get_objects())
        {
            json message;

            switch (obj->get_type())
            {
            case HAILO_DETECTION:
            {
                HailoDetectionPtr detection = std::dynamic_pointer_cast<HailoDetection>(obj);

                if (detection->get_label() == "person")
                {
                    auto detection_bbox = detection->get_bbox();

                    float zone_xmin = 0.6;
                    float zone_ymin = 0.2;
                    float zone_xmax = 0.9;
                    float zone_ymax = 0.7;

                    HailoBBox roi_bbox = hailo_common::create_flattened_bbox(roi->get_bbox(), roi->get_scaling_bbox());
                    auto xmin = (detection_bbox.xmin() * roi_bbox.width()) + roi_bbox.xmin();
                    auto ymin = (detection_bbox.ymin() * roi_bbox.height()) + roi_bbox.ymin();
                    auto xmax = (detection_bbox.xmax() * roi_bbox.width()) + roi_bbox.xmin();
                    auto ymax = (detection_bbox.ymax() * roi_bbox.height()) + roi_bbox.ymin();

                    message["class"] = "person";
                    message["bbox"] = {
                        {"xmin", detection_bbox.xmin()},
                        {"ymin", detection_bbox.ymin()},
                        {"xmax", detection_bbox.xmax()},
                        {"ymax", detection_bbox.ymax()}};
                    message["zone_status"] = is_intrusion(xmin, ymin, xmax, ymax, zone_xmin, zone_ymin, zone_xmax, zone_ymax)
                                                 ? "in_zone"
                                                 : "not_in_zone";
                }
                else if (detection->get_label() == "face")
                {
                    auto detection_bbox = detection->get_bbox();

                    message["class"] = "face";
                    message["bbox"] = {
                        {"xmin", detection_bbox.xmin()},
                        {"ymin", detection_bbox.ymin()},
                        {"xmax", detection_bbox.xmax()},
                        {"ymax", detection_bbox.ymax()}};

                    auto landmark = detection->get_objects_typed(HAILO_LANDMARKS);
                    if (!landmark.empty())
                    {
                        HailoLandmarksPtr landmarks = std::dynamic_pointer_cast<HailoLandmarks>(landmark[0]);
                        if (landmarks)
                        {
                            message["landmarks"] = json::array();
                            for (const auto &point : landmarks->get_points())
                            {
                                message["landmarks"].push_back({{"x", point.x()},
                                                                {"y", point.y()}});
                            }
                        }
                    }
                }
                break;
            }
            }

            if (!message.empty())
            {
                all_messages.push_back(message); // accumulate this object message
            }
        }

        // Send only once, if any messages exist
        if (!all_messages.empty())
        {
            zmq::message_t zmq_msg(all_messages.dump());
            zmq_publisher.send(zmq_msg, zmq::send_flags::none);
        }

        data->add_time_stamp(m_stage_name);
        set_duration(data);
        send_to_subscribers(data);

        return AppStatus::SUCCESS;
    }
};
