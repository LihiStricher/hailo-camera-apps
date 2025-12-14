#pragma once

// General includes
#include <iostream>
#include <cstring>

// Infra includes
#include "stage.hpp"
#include "buffer.hpp"
#include "hailo_tensors.hpp"

#include "zmq.hpp"

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

    AppStatus process(BufferPtr data) override
    {
        if (!init_done)
        {
            init_zmq_publisher("tcp://10.0.0.1:7000");
            init_done = true;
        }

        auto roi = data->get_roi();
        
        // Extract and send output tensors
        auto tensors = roi->get_tensors();
        if (tensors.empty())
        {
            std::cout << "WARNING: No tensors found in ROI!" << std::endl;
        }
        
        for (const auto &tensor : tensors)
        {
            // Get tensor data as uint16 (since network outputs HAILO_FORMAT_TYPE_UINT16)
            uint16_t *tensor_data = reinterpret_cast<uint16_t *>(tensor->data());
            // Calculate correct byte size: size() returns element count, multiply by 2 for uint16
            size_t tensor_size_bytes = tensor->is_uint16() ? (tensor->size() * sizeof(uint16_t)) : tensor->size();
            
            // Send tensor name first
            std::string tensor_name = tensor->name();
            zmq_publisher.send(zmq::buffer(tensor_name), zmq::send_flags::sndmore);
            
            // Send tensor data via ZeroMQ
            zmq_publisher.send(zmq::buffer(tensor_data, tensor_size_bytes), zmq::send_flags::none);
        }

        data->add_time_stamp(m_stage_name);
        set_duration(data);
        send_to_subscribers(data);

        return AppStatus::SUCCESS;
    }
};
