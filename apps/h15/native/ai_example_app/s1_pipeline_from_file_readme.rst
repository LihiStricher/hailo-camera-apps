
=====================
S1 Pipeline from File
=====================

Overview
========

This case study demonstrates an advanced video processing pipeline that reads video from a file, performs AI-based detection and post-processing, overlays results, encodes the video, and streams it over UDP. The pipeline consists of the following stages:

.. code-block:: text

    FileSourceStage -> ResizeStage -> HailortAsyncStage (YOLO Detection) -> PostprocessStage -> AggregatorStage -> OverlayStage -> EncoderStage -> UdpStage

Pipeline Architecture
====================

.. code-block:: text
┌─────────────────────┐   ┌───────────────┐   ┌─────────────────────────┐   ┌──────────────────────┐   ┌─────────────────────┐   ┌───────────────┐   ┌──────────────────┐   ┌─────────────┐
│ FileSourceStage     │──▶│ ResizeStage   │──▶│ HailortAsyncStage       │──▶│ PostprocessStage     │──▶│ AggregatorStage     │──▶│ OverlayStage  │──▶│ EncoderStage     │──▶│ UdpStage    │
└─────────────────────┘   └───────────────┘   └─────────────────────────┘   └──────────────────────┘   └─────────────────────┘   └───────────────┘   └──────────────────┘   └─────────────┘

Stage Descriptions
==================

- **FileSourceStage**: Reads raw NV12 video frames from a file.
- **ResizeStage**: Resizes frames for object detection.
- **HailortAsyncStage (YOLO Detection)**: Runs YOLOv7 AI inference for object detection.
- **PostprocessStage**: Applies post-processing to AI results using a custom shared library.
- **AggregatorStage**: Aggregates detection results and prepares them for overlay.
- **OverlayStage**: Draws detection results and landmarks on video frames.
- **EncoderStage**: Encodes frames using a configurable encoder.
- **UdpStage**: Streams the encoded video over UDP to a target host and port.

Usage
=====

Required Parameters
-------------------

- ``-f, --file`` – Video file path (NV12 format)
- ``-w, --width`` – Video width in pixels
- ``-g, --height`` – Video height in pixels
- ``-e, --encoder-config`` – Encoder configuration file path

Optional Parameters
------------------

- ``-r, --fps`` – Frame rate (default: 30)
- ``-o, --host`` – Target IP address (default: 10.0.0.2)
- ``-p, --port`` – UDP port (default: 5000)
- ``-t, --timeout`` – Runtime timeout in seconds (default: 60)
- ``-s, --print-fps`` – Enable FPS statistics (default: false)

Basic Usage
-----------

.. code-block:: bash

    # Get help
    ./s1_pipeline_from_file --help

    # Run with required parameters
    ./s1_pipeline_from_file -f video.nv12 -w 1920 -g 1080 -e encoder.json


Examples
--------

.. code-block:: bash

    # Basic usage with required parameters
    ./s1_pipeline_from_file -f /path/to/video.nv12 -w 1920 -g 1080 -e /path/to/encoder.json

    # Custom network settings
    ./s1_pipeline_from_file -f video.nv12 -w 1920 -g 1080 -e encoder.json -o 192.168.1.100 -p 6000

    # Custom resolution and frame rate
    ./s1_pipeline_from_file -f video.nv12 -w 1280 -g 720 -e encoder.json -r 25

    # Enable FPS monitoring
    ./s1_pipeline_from_file -f video.nv12 -w 1920 -g 1080 -e encoder.json -s

    # Run for 5 minutes
    ./s1_pipeline_from_file -f video.nv12 -w 1920 -g 1080 -e encoder.json -t 300


Input Requirements

Video File Format
-----------------

- **Format**: Raw NV12 (YUV 4:2:0)
- **Resolution**: Any (specify with ``-w`` and ``-g``)
- **Frame Rate**: Any (specify with ``-r``)

Creating Test Video Files
------------------------

.. code-block:: bash

    # Convert MP4 to NV12 using FFmpeg
    ffmpeg -i input.mp4 -pix_fmt nv12 -f rawvideo output.nv12

    # Create test pattern (colored bars)
    ffmpeg -f lavfi -i testsrc=duration=10:size=1920x1080:rate=30 \
           -pix_fmt nv12 -f rawvideo test_video.nv12

