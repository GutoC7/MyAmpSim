# My Amp Sim 🎸

A real-time, low-latency Guitar Amplifier Simulator and Multi-Effects suite built in C++ using the JUCE Framework. 

I developed this project to bridge the gap between Computer Science and Music (also to learn more and keep my coding skills sharp), diving deep into Digital Signal Processing (DSP) to build an architecture-grade audio plugin from scratch.

## 🎛️ Features

* **Tube Distortion Engine:** Custom waveshaping using hyperbolic tangent (`tanh`) for warm, analog-style soft clipping.
* **Convolution Cabinet Simulator:** Integrated Impulse Response (IR) loader powered by the JUCE DSP module to replicate the frequency response of a physical 12" speaker cabinet.
* **LFO Tremolo:** Sine-wave driven volume modulation.
* **Tape Echo (Delay):** Custom-built Circular Buffer implementation for delay lines, featuring linear-interpolated parameter smoothing to eliminate "zipper" artifacts during real-time manipulation.
* **Standalone & VST3:** Runs as a standalone application for live playing or as a VST3 plugin inside any major DAW (Ableton, Reaper, Logic, etc.).

## 🛠️ Tech Stack & Architecture

* **Language:** C++17/20
* **Framework:** [JUCE](https://juce.com/)
* **Build System:** CMake
* **DSP Concepts Implemented:** 
  * Separation of Concerns (Stateless processing functions vs. Stateful objects)
  * Memory Management (Real-time safe circular buffers, pre-allocation to prevent audio dropouts)
  * Atomic Variables for thread-safe GUI-to-Audio communication
  * Parameter Smoothing (One-pole low-pass filtering on control signals)

## 🚀 Build Instructions (Windows)

### Prerequisites
1. **Visual Studio 2022** with the **"Desktop development with C++"** workload installed.
2. Ensure **"C++ CMake tools for Windows"** is checked in the Visual Studio Installer.
3. [CMake](https://cmake.org/download/) installed and added to your system PATH.

### Compiling from Source
1. Clone the repository:
   ```bash
   git clone [https://github.com/GutoC7/MyAmpSim.git](https://github.com/GutoC7/MyAmpSim.git)
   cd MyAmpSim