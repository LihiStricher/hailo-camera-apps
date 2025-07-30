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
#include "zmq.hpp"

#include <numeric> // for accumulate
#include <cmath>   // for sqrt

// HailoRT includes
#include "hailo/hailort.hpp"

// Tappas includes
#include "hailo_objects.hpp"

// Media library includes
#include "media_library/media_library_types.hpp"

// Infra includes
#include "buffer.hpp"
#include "queue.hpp"
#include "stage.hpp"

/**
 * @brief Class representing an asynchronous HailoRT stage in a connected stage pipeline.
 * 
 * This class is responsible for managing the HailoRT inference model, setting up
 * buffer pools, and executing asynchronous inference jobs.
 */
class FireDetectionHailortAsyncStage : public ConnectedStage
{
private:
    // pool members
    int m_output_pool_size;  ///< Size of the output buffer pool.
    std::unordered_map<std::string, MediaLibraryBufferPoolPtr> m_tensor_buffer_pools; ///< Buffer pools for each output tensor.
    
    // hailort members
    std::unique_ptr<hailort::VDevice> m_vdevice;    ///< HailoRT virtual device.
    std::shared_ptr<hailort::InferModel> m_infer_model; ///< HailoRT inference model.
    hailort::ConfiguredInferModel m_configured_infer_model; ///< Configured HailoRT inference model.
    hailort::ConfiguredInferModel::Bindings m_bindings; ///< Bindings for connecting buffers to the inference model.
    std::unordered_map<std::string, hailo_vstream_info_t> m_vstream_infos; ///< Information about each virtual stream.
    std::shared_ptr<hailort::AsyncInferJob> m_last_infer_job; ///< Pointer to the last asynchronous inference job.

    // network members
    std::string m_hef_path; ///< Path to the Hailo Execution File (HEF).
    std::string m_group_id; ///< Group ID for the HailoRT device.
    int m_batch_size;   ///< Batch size for inference.
    int m_scheduler_threshold; ///< Threshold for the scheduler.
    bool m_dynamic_threshold; ///< Whether to use dynamic thresholding.
    std::chrono::milliseconds m_scheduler_timeout; ///< Timeout for the scheduler.

    std::atomic<size_t> m_active_jobs;  ///< Number of active inference jobs.
    size_t m_jobs_limit; ///< Limit on the number of active inference jobs.
    std::mutex m_active_jobs_mutex; ///< Mutex for the active jobs counter.
    std::condition_variable m_active_jobs_cv; ///< Condition variable for the active jobs counter.
    std::condition_variable m_available_buffers_cv;
    std::mutex m_buff_pool_mutex;
    StagePoolMode m_pool_mode; //< Pool mode for the buffer pool used in this stage

    bool init_done = false;
    zmq::context_t zmq_context;
    zmq::socket_t zmq_publisher;

    std::vector<std::vector<float>> text_embeddings;

public:
    /**
     * @brief Construct a new HailortAsyncStage object.
     * 
     * @param name Name of the stage.
     * @param hef_path Path to the Hailo Execution File (HEF).
     * @param queue_size Size of the processing queue.
     * @param output_pool_size Size of the output buffer pool.
     * @param group_id Group ID for the HailoRT device.
     * @param batch_size Batch size for inference.
     * @param scheduler_threshold Threshold for the scheduler.
     * @param scheduler_timeout Timeout for the scheduler.
     * @param print_fps Whether to print frames per second information.
     */
    FireDetectionHailortAsyncStage(std::string name, std::string hef_path, size_t queue_size, int output_pool_size, std::string group_id, int batch_size, size_t job_limit,
                                   int scheduler_threshold = 4, bool dynamic_threshold = false, std::chrono::milliseconds scheduler_timeout = std::chrono::milliseconds(100), bool print_fps = false,
                                   StagePoolMode pool_mode = StagePoolMode::FAIL_ON_EMPTY_POOL) : ConnectedStage(name, queue_size, false, print_fps), m_output_pool_size(output_pool_size), m_hef_path(hef_path), m_group_id(group_id), m_batch_size(batch_size),
                                                                                                  m_scheduler_threshold(scheduler_threshold), m_dynamic_threshold(dynamic_threshold), m_scheduler_timeout(scheduler_timeout), m_jobs_limit(job_limit), m_pool_mode(pool_mode)
    {
        m_last_infer_job = nullptr;
        m_active_jobs = 0;
    }

    /**
     * @brief Initialize the HailoRT stage.
     * 
     * @return AppStatus Status of the initialization.
     */
    AppStatus init() override
    {
        hailo_vdevice_params_t vdevice_params = {0};
        hailo_init_vdevice_params(&vdevice_params);
        vdevice_params.group_id = m_group_id.c_str();
        m_debug_counters = std::make_shared<AIStageCounters>(m_stage_name);

        // Create a vdevice
        auto vdevice_exp = hailort::VDevice::create(vdevice_params);
        if (!vdevice_exp) {
            std::cerr << "Failed create vdevice, Hailort status = " << vdevice_exp.status() << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Failed create vdevice, Hailort status = {}",  vdevice_exp.status());
            return AppStatus::HAILORT_ERROR;
        }
        m_vdevice = vdevice_exp.release();

        // Create an infer model
        auto infer_model_exp = m_vdevice->create_infer_model(m_hef_path.c_str());
        if (!infer_model_exp) {
            std::cerr << "Failed to create infer model, Hailort status = " << infer_model_exp.status() << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Failed to create infer model, Hailort status = {}", infer_model_exp.status());
            return AppStatus::HAILORT_ERROR;
        }
        m_infer_model = infer_model_exp.release();
        m_infer_model->set_batch_size(m_batch_size);
        for(auto output : m_infer_model->outputs()) {
            output.set_format_type(hailo_format_type_t::HAILO_FORMAT_TYPE_FLOAT32);
        }

        // Configure the infer model
        auto configured_infer_model_exp = m_infer_model->configure();
        if (!configured_infer_model_exp) {
            std::cerr << "Failed to create configured infer model, Hailort status = " << configured_infer_model_exp.status() << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Failed to create configured infer model, Hailort status = {}", configured_infer_model_exp.status());
            return AppStatus::HAILORT_ERROR;
        }
        m_configured_infer_model = configured_infer_model_exp.release();
        m_configured_infer_model.set_scheduler_threshold(m_scheduler_threshold);
        m_configured_infer_model.set_scheduler_timeout(std::chrono::milliseconds(m_scheduler_timeout));
        
        // Create bindings through which to connect buffers for inference
        auto bindings = m_configured_infer_model.create_bindings();
        if (!bindings) {
            std::cerr << "Failed to create infer bindings, Hailort status = " << bindings.status() << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Failed to create infer bindings, Hailort status = {}", bindings.status());
            return AppStatus::HAILORT_ERROR;
        }
        m_bindings = bindings.release();

        // Prepare a buffer pool for each output tensor
        for (auto &output : m_infer_model->outputs()) {
            size_t tensor_size = output.get_frame_size();
            std::string tensor_name = m_stage_name + "/" + output.name();
            m_tensor_buffer_pools[output.name()] = std::make_shared<MediaLibraryBufferPool>(tensor_size, 1, HAILO_FORMAT_GRAY8,
                                                                                             m_output_pool_size, HAILO_MEMORY_TYPE_DMABUF, tensor_size, tensor_name);
            if (m_tensor_buffer_pools[output.name()]->init() != MEDIA_LIBRARY_SUCCESS)
            {
                return AppStatus::BUFFER_ALLOCATION_ERROR;
            }
        }

        // Gather the vstream info for each output tensor
        auto vstream_infos = m_infer_model->hef().get_output_vstream_infos();
        if (!vstream_infos) {
            std::cerr << "Failed to get vstream info, Hailort status = " << vstream_infos.status() << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Failed to get vstream info, Hailort status = {}", vstream_infos.status());
            return AppStatus::HAILORT_ERROR;
        }
        for (const auto &vstream_info : vstream_infos.value()) {
            m_vstream_infos[vstream_info.name] = vstream_info;
        }

        return AppStatus::SUCCESS;
    }

    /**
     * @brief Deinitialize the HailoRT stage.
     * 
     * @return AppStatus Status of the deinitialization.
     */
    AppStatus deinit() override
    {
        // Wait for last infer to finish
        if (m_last_infer_job) {
            auto status = m_last_infer_job->wait(std::chrono::milliseconds(10000));
            if (HAILO_SUCCESS != status) {
                std::cerr << "Failed to wait for infer to finish, status = " << status << std::endl;
                REFERENCE_CAMERA_LOG_ERROR("Failed to wait for infer to finish, status = {}", status);
                return AppStatus::HAILORT_ERROR;
            }
        }
        for (auto &queue : m_queues)
        {
            queue->flush();
        }

        return AppStatus::SUCCESS;
    }

     /**
     * @brief Acquire and set tensor buffers for the inference output.
     * 
     * @param tensor_buffers Map of tensor buffers to be acquired and set.
     * @return AppStatus Status of acquiring and setting the tensor buffers.
     */
    AppStatus acquire_and_set_tensor_buffers(std::unordered_map<std::string, BufferPtr> &tensor_buffers)
    {
        // Acquire a buffer for each output tensor
        for (auto &output : m_infer_model->outputs()) {
            // Acquire a buffer for this tensor output from the corresponding buffer pool
            HailoMediaLibraryBufferPtr tensor_buffer = std::make_shared<hailo_media_library_buffer>();
            BufferPtr tensor_buffer_ptr = std::make_shared<Buffer>(tensor_buffer);
            if (m_tensor_buffer_pools[output.name()]->acquire_buffer(tensor_buffer) != MEDIA_LIBRARY_SUCCESS)
            {
                m_debug_counters->increment_failed_acquire_buffer();
                if (m_pool_mode == StagePoolMode::FAIL_ON_EMPTY_POOL) {
                    return AppStatus::BUFFER_ALLOCATION_ERROR;
                } else if (m_pool_mode == StagePoolMode::BLOCKING) {
                    REFERENCE_CAMERA_LOG_INFO("{} acquire buffer from buffer pool failed, wait for available buffer", m_stage_name);
                    std::unique_lock<std::mutex> lock(m_buff_pool_mutex);
                    m_available_buffers_cv.wait(lock, [this, output, tensor_buffer] { return m_tensor_buffer_pools[output.name()]->acquire_buffer(tensor_buffer) == MEDIA_LIBRARY_SUCCESS; });
                } else {
                    for (auto& buffer : tensor_buffers) {
                        buffer.second.reset();
                        m_debug_counters->increment_dropped_frames();
                    }
                    return AppStatus::SUCCESS;
                }
            }

            // Set entry in map
            tensor_buffers[output.name()] = tensor_buffer_ptr;

            // Set the HailoRT bindings for the acquired buffer
            size_t tensor_size = output.get_frame_size();
            auto status = m_bindings.output(output.name())->set_buffer(hailort::MemoryView(tensor_buffer->get_plane_ptr(0), tensor_size));
            if (HAILO_SUCCESS != status) {
                std::cerr << m_stage_name << " failed to set infer output buffer "<< output.name() << ", Hailort status = " << status << std::endl;
                REFERENCE_CAMERA_LOG_ERROR("{} failed to set infer output buffer {} , Hailort status = ", m_stage_name, output.name(), status);
                return AppStatus::HAILORT_ERROR;
            }
        }
        return AppStatus::SUCCESS;
    }

    // Normalize a vector in-place
    void normalize(std::vector<float> &vec)
    {
        float norm = 0.0f;
        for (float v : vec)
        {
            norm += v * v;
        }
        norm = std::sqrt(norm);
        if (norm > 0.0f)
        {
            for (float &v : vec)
            {
                v /= norm;
            }
        }
    }

    // Dot product
    float dot_product(const std::vector<float> &a, const std::vector<float> &b)
    {
        float result = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
        {
            result += a[i] * b[i];
        }
        return result;
    }

    // Softmax
    std::vector<float> softmax(const std::vector<float> &logits)
    {
        std::vector<float> exp_vals(logits.size());
        float max_logit = *std::max_element(logits.begin(), logits.end());
        float sum = 0.0f;

        for (size_t i = 0; i < logits.size(); ++i)
        {
            exp_vals[i] = std::exp(logits[i] - max_logit);
            sum += exp_vals[i];
        }

        for (float &val : exp_vals)
        {
            val /= sum;
        }

        return exp_vals;
    }

    // Main calculation
    std::pair<std::string, float> calc_probs(
        void *raw_image_embedding, // now a void pointer
        size_t embedding_size,     // number of float32 values in the buffer
        const std::vector<std::vector<float>> &normalized_text_features)
    {
        // Convert void* to std::vector<float>
        float *float_ptr = reinterpret_cast<float *>(raw_image_embedding);
        std::vector<float> image_embedding(float_ptr, float_ptr + embedding_size);

        normalize(image_embedding); // Normalize only image embedding

        // Compute logits (cosine similarity)
        std::vector<float> logits;
        for (const auto &text_feat : normalized_text_features)
        {
            logits.push_back(100.0f * dot_product(image_embedding, text_feat));
        }

        // Apply softmax
        std::vector<float> probs = softmax(logits);

        // Get best match
        size_t best_index = 0;
        float best_prob = -1.0f;
        for (size_t i = 0; i < probs.size(); ++i)
        {
            if (probs[i] > best_prob)
            {
                best_prob = probs[i];
                best_index = i;
            }
        }

        return {std::to_string(best_index), best_prob * 100.0f};
    }

    /**
     * @brief Perform inference on the input buffer by triggering an async infer job.
     * This job calls the given callback on completion.
     * 
     * @param input_buffer Buffer containing the input data.
     * @param tensor_buffers Map of tensor buffers for the inference output.
     * @return AppStatus Status of the inference.
     */    
    AppStatus infer(BufferPtr input_buffer, const std::unordered_map<std::string, BufferPtr> &tensor_buffers)
    {
        // wait for infer model to be ready
        auto status = m_configured_infer_model.wait_for_async_ready(std::chrono::milliseconds(1000));
        if (HAILO_SUCCESS != status) {
            std::cerr << "Failed to wait for async ready, Hailort status = " << status << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Failed to wait for async ready, Hailort status = {}", status);
            return AppStatus::HAILORT_ERROR;
        }

        // Run the async infer api, when inference is done it will call the given callback
        std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
        auto job = m_configured_infer_model.run_async(m_bindings, [tensor_buffers, input_buffer, begin, this](const hailort::AsyncInferCompletionInfo& completion_info) {
            // active job finished
            --this->m_active_jobs;
            m_active_jobs_cv.notify_one();
            
            // check infer status
            if (completion_info.status != HAILO_SUCCESS) {
                std::cerr << "Failed to run async infer, Hailort status = " << completion_info.status << std::endl;
                REFERENCE_CAMERA_LOG_ERROR("Failed to run async infer, Hailort status = {}", completion_info.status);
                return AppStatus::HAILORT_ERROR;
            }

            if (m_end_of_stream)
                return AppStatus::SUCCESS;

            // Add metadata for each output tensor buffer
            for (auto &output : m_infer_model->outputs()) {
                m_debug_counters->increment_extra_counter(static_cast<int>(AIExtraCounters::TENSORS));
                BufferPtr tensor_buffer = tensor_buffers.at(output.name());
                void* data_ptr = tensor_buffer->get_buffer()->get_plane_ptr(0);
                size_t embedding_size = tensor_buffer->get_buffer()->get_plane_size(0) / sizeof(float);
                auto [best_index, confidence] = calc_probs(data_ptr, embedding_size, text_embeddings);
                //send the best index and confidence to the ZMQ publisher
                std::string message = best_index + "," + std::to_string(confidence);
                zmq_publisher.send(zmq::buffer(message), zmq::send_flags::none);

                TensorMetadataPtr tensor_metadata = std::make_shared<TensorMetadata>(tensor_buffer, output.name());
                input_buffer->add_metadata(tensor_metadata);

                // Add the vstream info and data pointer to the HailoRoi for later use (postprocessing)
                input_buffer->get_roi()->add_tensor(std::make_shared<HailoTensor>(reinterpret_cast<uint8_t *>(tensor_buffer->get_buffer()->get_plane_ptr(0)), 
                                                                                  m_vstream_infos[output.name()]));
            }

            std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
            if (m_print_fps)
            {
                std::cout << "Inference time (" << m_stage_name << ") = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() << "[microseconds]" << std::endl;
            }
            
            // Send the input buffer to the next stage
            input_buffer->add_time_stamp(m_stage_name);
            set_duration(input_buffer);
            m_debug_counters->increment_output_frames();
            send_to_subscribers(input_buffer);

            return AppStatus::SUCCESS;
        });
        ++m_active_jobs;

        if (!job) {
            std::cerr << "Failed to start async infer job, status = " << job.status() << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Failed to start async infer job, status = {}", job.status());
            return AppStatus::HAILORT_ERROR;
        }

        // detach the job to run in it's own thread on the side
        job->detach();
        m_last_infer_job = std::make_shared<hailort::AsyncInferJob>(job.release());

        return AppStatus::SUCCESS;
    }

    std::vector<std::vector<float>> read_text_features(const std::string &filename, int feature_dim)
    {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file)
        {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (file_size % (feature_dim * sizeof(float)) != 0)
        {
            throw std::runtime_error("File size is not a multiple of feature_dim * sizeof(float)");
        }

        int num_vectors = file_size / (feature_dim * sizeof(float));
        std::vector<std::vector<float>> result(num_vectors, std::vector<float>(feature_dim));

        for (int i = 0; i < num_vectors; ++i)
        {
            file.read(reinterpret_cast<char *>(result[i].data()), feature_dim * sizeof(float));
            if (!file)
            {
                throw std::runtime_error("Error reading feature vector " + std::to_string(i));
            }
        }

        return result;
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

    /**
     * @brief Process the data in the buffer.
     * 
     * @param data Buffer containing the data to be processed.
     * @return AppStatus Status of the processing.
     */
    AppStatus process(BufferPtr data)
    {
        if (!init_done)
        {
            init_zmq_publisher("tcp://10.0.0.1:5555");
            std::string path = "/home/root/apps/s1_demo/resources/norm_text_features.raw";
            int feature_dim = 640;
            text_embeddings = read_text_features(path, feature_dim);
            init_done = true;
        }
        m_debug_counters->increment_input_frames();
        // Wait and set scheduler threshold if dynamic thresholding used
        if (m_dynamic_threshold)
        {
            std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::BATCH);
            if (metadata.size() > 0)
            {
                BatchMetadataPtr batch_metadata = std::dynamic_pointer_cast<BatchMetadata>(metadata[0]);
                
                // if this is the start of a new batch, wait for the last infer job to finish
                if (batch_metadata->get_index() == 0)
                {
                    // wait for acttive jobs to finish
                    std::unique_lock<std::mutex> lock(m_active_jobs_mutex);
                    m_active_jobs_cv.wait(lock, [this] { return m_active_jobs == 0 || m_end_of_stream; });
                    if (m_end_of_stream)
                        return AppStatus::SUCCESS;
                    m_configured_infer_model.set_scheduler_threshold(batch_metadata->get_total_size());
                }
            }
        }
        
        // wait for available jobs
        std::unique_lock<std::mutex> lock(m_active_jobs_mutex);
        m_active_jobs_cv.wait(lock, [this]
                              { return m_active_jobs < m_jobs_limit; });

        m_bindings.input("clip_convnext_visual_256/input_layer1")->set_buffer(hailort::MemoryView(data->get_buffer()->get_plane_ptr(0), data->get_buffer()->get_plane_size(0) + data->get_buffer()->get_plane_size(1)));

        // Acquire and set tensor buffers
        std::unordered_map<std::string, BufferPtr> tensor_buffers;
        if (acquire_and_set_tensor_buffers(tensor_buffers) != AppStatus::SUCCESS)
        {
            return AppStatus::HAILORT_ERROR;
        }

        // Run the inference
        if (infer(data, tensor_buffers) != AppStatus::SUCCESS)
        {
            return AppStatus::HAILORT_ERROR;
        }
        
        return AppStatus::SUCCESS;
    }
};
