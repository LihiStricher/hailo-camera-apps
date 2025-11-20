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
        std::cout << "OutputMetadataStage: Sending " << tensors.size() << " tensors via ZMQ." << std::endl;
        
        // Debug: print ROI info
        std::cout << "OutputMetadataStage: ROI object = " << roi.get() << std::endl;
        
        if (tensors.empty())
        {
            std::cout << "WARNING: No tensors found in ROI!" << std::endl;
        }
        
        for (const auto &tensor : tensors)
        {
            // Get tensor data
            uint8_t *tensor_data = tensor->data();
            size_t tensor_size = tensor->size();
            
            // Send tensor name first
            std::string tensor_name = tensor->name();
            std::cout << "Sending tensor: " << tensor_name << " (size: " << tensor_size << ")" << std::endl;
            zmq_publisher.send(zmq::buffer(tensor_name), zmq::send_flags::sndmore);
            
            // Send tensor data via ZeroMQ
            zmq_publisher.send(zmq::buffer(tensor_data, tensor_size), zmq::send_flags::none);
        }

        data->add_time_stamp(m_stage_name);
        set_duration(data);
        send_to_subscribers(data);

        return AppStatus::SUCCESS;
    }
};
