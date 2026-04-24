# Source to WebSocket Plugin for OBS

A high-performance OBS plugin designed to bridge media content and metadata from OBS to a centralized WebSocket server. Part of the **mediaWarp** ecosystem.

## Features

- **Frame to WebSocket Filter**: Captures video frames from any source, scales them, compresses them to JPEG, and transmits them as **Base64-encoded strings** to dedicated topics (e.g., `frame/my_source`).
- **Text to WebSocket Filter**: Monitors OBS text sources and transmits changes to custom topics.
- **Topic-Based Routing**: Uses the `media_warp_transmit_topic` signal to route data through the local bridge, ensuring low-latency delivery to specific overlay subscribers.
- **Built-in Debuggers**: Updated HTML test pages (`frame_test.html`) that use the **Unified Port Architecture**.

## Installation

### Prerequisites
- OBS Studio 30.0 or newer
- macOS (Apple Silicon recommended)
- `libjpeg-turbo` (installed via Homebrew: `brew install jpeg-turbo`)

### Build and Deploy
1. Clone the repository into the `mediaWarp` workspace.
2. Run the deployment script:
   ```bash
   ./build_and_deploy.sh
   ```
3. Restart OBS.

## Usage

### Frame to WebSocket
1. Right-click any source in OBS and select **Filters**.
2. Add the **"frame to websocket"** filter.
3. Configure:
   - **WebSocket Type**: A unique identifier for the stream (e.g., `camera_main`).
   - **Target FPS**: Frame rate for transmission (default: 15).
   - **Scale Width**: Target width in pixels (default: 256).

### Text to WebSocket
1. Right-click a text source and select **Filters**.
2. Add the **"text to websocket"** filter.
3. Configure:
   - **Setting Key**: The internal OBS setting key for the text (usually `text`).
   - **WebSocket Topic**: The topic identifier (e.g., `lyrics_overlay`).

## Debugging

The plugin includes diagnostic tools located in the bundle's `Resources` directory:

- **`frame_test.html`**: Visualizes the captured video frames and displays latency/bandwidth metrics.
- **`text_test.html`**: Displays real-time text updates and a history of changes.

These pages automatically attempt to connect to the local mediaWarp WebSocket bridge.

## Technical Details

- **Language**: C++
- **Dependencies**: `libobs`, `libjpeg-turbo`, `Accelerate` (macOS).
- **Architecture**:
  - Video processing is optimized using Apple's `vImage` for scaling.
  - JPEG compression is handled by `libjpeg-turbo` on a per-thread basis for high throughput.
  - Data transmission is non-blocking via OBS signal handlers.
