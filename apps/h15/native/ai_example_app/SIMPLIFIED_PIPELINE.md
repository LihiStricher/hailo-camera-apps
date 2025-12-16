# Simplified S1 Pipeline (v1.7.0) - Architecture Diagram

## Overall Pipeline Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          FRONTEND (Media Library)                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                         │
│  │  sink0 4K   │  │  sink1 4K   │  │  sink2 FHD  │                         │
│  │  Vision     │  │  Overlay    │  │  AI Input   │                         │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘                         │
└─────────┼─────────────────┼─────────────────┼──────────────────────────────┘
          │                 │                 │
          │                 │                 │
          ├─────────────────┤                 │
          │                 │                 │
          ▼                 ▼                 ▼
      ┌────────────────────────────┐   ┌──────────────────┐
      │      MUXER STAGE           │   │  TILLING STAGE   │
      │  (AI_VISION_SINK +         │   │  (Person Det)    │
      │   SECONDARY_VISION_SINK)   │   │  1920x1080       │
      └────────────┬───────────────┘   └────────┬─────────┘
                   │                            │
                   ▼                            ▼
              ┌──────────────┐          ┌──────────────────┐
              │ CALLBACK     │          │  YOLO DETECTION  │
              │ STAGE        │          │  (HailortAsync)  │
              └──────┬───────┘          └────────┬─────────┘
                     │                           │
                     ▼                           ▼
           ┌─────────────────────┐      ┌──────────────────┐
           │ RESULTS AGGREGATOR  │      │  YOLO POST-PROC  │
           │ (merges streams)    │      │  (Postprocess)   │
           └──────────┬──────────┘      └────────┬─────────┘
                      │                          │
                      └──────────┬───────────────┘
                                 │
                                 ▼
                         ┌────────────────────┐
                         │  AFTER-RESIZE AGG  │
                         │  (merges detections)
                         └────────┬───────────┘
                                  │
                                  ▼
                            ┌──────────────┐
                            │   TEE STAGE  │
                            │  (split flow)│
                            └──────┬───────┘
                                   │
                                   ▼
                         ┌─────────────────────┐
                         │  STAGE 2 AGGREGATOR │
                         │  (final merge)      │
                         └──────────┬──────────┘
                                    │
                                    ▼
                            ┌─────────────────┐
                            │  TRACKER STAGE  │
                            │  (Persistent)   │
                            └────────┬────────┘
                                     │
                                     ▼
                            ┌─────────────────┐
                            │  OVERLAY STAGE  │
                            │  (Draw results) │
                            └────────┬────────┘
                                     │
                                     ▼
                            ┌─────────────────┐
                            │  ENCODER STAGE  │
                            │  (H.264 encode) │
                            └────────┬────────┘
                                     │
                                     ▼
                            ┌─────────────────┐
                            │   UDP OUTPUT    │
                            │  (stream out)   │
                            └─────────────────┘
```

## Detailed Pipeline Connections

### Vision Stream Path (sink0 + sink1)
```
sink0 (4K Vision) ─┐
                   ├─→ MUXER ─→ CALLBACK ─→ RESULTS_AGG ─┐
sink1 (Overlay) ──┘                                       │
                                                          ├─→ TEE ─→ STAGE_2_AGG
sink2 (FHD AI) ─→ TILLING ─→ YOLO ─→ POST ─→ AFTER_RESIZE_AGG ─┘
                                                    ▲
                                                    │
                                              Sends detections
```

### Output Stream Path
```
STAGE_2_AGG ─→ TRACKER ─→ OVERLAY ─→ ENCODER ─→ UDP_OUTPUT
```

## Stage Configuration Summary

| Stage | Type | Input | Output | Purpose |
|-------|------|-------|--------|---------|
| **MUXER** | MuxerStage | sink0, sink1 | CALLBACK | Combines 4K vision streams |
| **CALLBACK** | CallbackStage | MUXER | RESULTS_AGG | Process/validate video |
| **TILLING** | TillingCropStage | sink2 (FHD) | YOLO | Crop and tile input for detection |
| **YOLO** | HailortAsyncStage | TILLING | POST | Person detection inference |
| **POST** | PostprocessStage | YOLO | AFTER_RESIZE_AGG | Parse YOLO output tensor |
| **AFTER_RESIZE_AGG** | AggregatorStage | TILLING, POST | RESULTS_AGG | Merge detection with original |
| **RESULTS_AGG** | AggregatorStage | CALLBACK, AFTER_RESIZE_AGG | TEE | Sync vision + detection |
| **TEE** | TeeStage | RESULTS_AGG | STAGE_2_AGG | Split data (single output) |
| **STAGE_2_AGG** | AggregatorStage | TEE | TRACKER | Final aggregation |
| **TRACKER** | PersistStage | STAGE_2_AGG | OVERLAY | Track detected persons |
| **OVERLAY** | OverlayStage | TRACKER | ENCODER | Draw boxes & landmarks |
| **ENCODER** | EncoderStage | OVERLAY | UDP | H.264 video encode |
| **UDP** | UdpStage | ENCODER | Network | Stream output |

## Key Parameters

### Detection Configuration
- **Model**: YOLOv5m_wo_spp_60p (person detection)
- **Input**: 1920×1080 FHD
- **HEF Path**: `/home/root/apps/s1_demo/resources/yolov5m_wo_spp_60p_nv12_fhd.hef`
- **Batch Size**: 5
- **Job Limit**: 10

### Output Configuration
- **Vision Sink**: sink0 (3840×2160 4K) - Primary output
- **AI Sink**: sink2 (1920×1080 FHD) - Internal only
- **Host IP**: 10.0.0.2
- **UDP Port**: 5000 + (sink_id * 2)

### Aggregator Timeouts
- **RESULTS_AGG**: 66ms (allow some latency)
- **STAGE_2_AGG**: 33-66ms (tight sync for overlay)
- **AFTER_RESIZE_AGG**: No timeout (detection sync)

## Removed Components (Simplified)

The following have been **removed** from the full pipeline:
- ❌ Face Detection (FACE_DETECTION_AI_STAGE)
- ❌ Face Landmarks (LANDMARKS_AI_STAGE)
- ❌ BBox Cropping for faces (BBOX_CROP_STAGE)
- ❌ Face detection post-processing
- ❌ Fire Detection (commented out)

This simplified version focuses purely on **person detection** and tracking.

## Data Flow Synchronization

```
Timeline:
├─ T=0ms: Frontend capture
├─ T=5ms: Muxer merges sink0, sink1
├─ T=10ms: YOLO detection (parallel)
├─ T=20ms: Post-processing
├─ T=25ms: RESULTS_AGG (wait for both paths)
├─ T=30ms: TEE split
├─ T=33ms: STAGE_2_AGG aggregation
├─ T=35ms: Tracker update
├─ T=40ms: Overlay drawing
├─ T=45ms: Encoder
└─ T=50ms: UDP transmission
```

**Critical Path**: Vision (5ms) + Detection (20ms) = ~25ms, plus aggregation overhead = ~35ms typical latency
