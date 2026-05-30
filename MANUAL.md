# 🎸 My Amp Sim - Official User Manual

Welcome to **My Amp Sim (V1.0)**, a hardware-accelerated, zero-latency digital guitar rig. This manual covers everything from basic signal routing to live-performance MIDI integration.

---

## 🎛️ 1. The User Interface
The UI is divided into three main sections:
1. **The Pedal Rack (Top):** 20 draggable buttons representing your signal chain.
2. **The Global Controls (Middle):** Master bypass, Tuner, MIDI Learn, and Preset management.
3. **The Active Pedal Screen (Bottom):** The main chassis where you adjust the parameters, view real-time graphs, or load custom IRs for the currently selected pedal.

---

## 🔗 2. The Dynamic Routing Matrix
Unlike physical pedalboards, you are not restricted by cables. My Amp Sim features a fully dynamic **20-slot routing matrix**.

* **To view a pedal:** Click any of the 20 buttons in the top rack to bring its controls to the main screen.
* **To rearrange your signal chain:** Click and hold a pedal in the top rack, drag it over another pedal, and release. The Audio Engine will instantly swap their positions in the DSP thread without dropping your audio.
* **To bypass a pedal:** Click the `[ PEDAL OFF ]` checkbox on the left side of the screen, or double-click the pedal's button in the rack. A glowing LED will turn red when a pedal is bypassed.

---

## 🎹 3. Hardware Integration (MIDI Learn)
My Amp Sim is designed for live performance. You can map any physical MIDI controller (expression pedals, mod-wheels, or keyboard knobs) to any knob on the screen.

**How to map a controller:**
1. Wiggle the physical knob or rock the expression pedal on your MIDI hardware.
2. Click the `[ MIDI LEARN ]` toggle on the UI.
3. Click and drag the knob on the screen you want to control (e.g., the Wah pedal sweep). 
4. Uncheck `[ MIDI LEARN ]`. Your hardware is now permanently bound to that parameter.

---

## 📊 4. Real-Time Audio Visualizer & Tuner
* **The Tuner:** Click `[ SHOW TUNER ]` to activate the heads-up display. The tuner utilizes an advanced **AMDF (Average Magnitude Difference Function)** algorithm for highly stable, low-latency pitch detection.
* **The EQ Visualizer:** Click the `EQ` pedal in the top rack. The dark glass screen features a dual-graph display:
  * **Cyan Graph (Top):** A real-time FFT Spectrum Analyzer showing your exact frequency curve.
  * **Green Graph (Bottom):** A time-domain Oscilloscope showing the physical shape of your audio waveform, allowing you to visually see when your signal begins to clip.

---

## 🗄️ 5. The Tone Vault (Presets)
My Amp Sim comes pre-loaded with over 65 historically accurate rigs, emulating iconic guitarists like Steve Vai, EVH, and Yngwie Malmsteen.

* **Load a Preset:** Click the blue `[ PRESETS ]` button to open the sliding side-drawer. Double-click any preset to instantly snap all 20 pedals to the correct settings and routing order.
* **Save a Preset:** Dial in your perfect tone and click the orange `[ SAVE PRESET ]` button. Your routing map and knob positions will be safely written to an XML file on your hard drive.
* **Reset:** Panic button. Click the dark red `[ RESET ALL SETTINGS ]` button to instantly wipe the board clean and return every parameter to default.

---

## 🎨 6. Customization
If you prefer a specific aesthetic, you can replace the dark studio gradient background with an image of your choice.
* Click the `[ BG ]` button in the bottom right corner to open your file explorer and load a `.jpg` or `.png`.
* Click the `[ X ]` button to clear it and return to the default metallic theme.

---

## 🎛️ 7. The Pedal Glossary
* **Gate / Comp / Boost:** Studio-grade dynamics processing.
* **Distortion:** Features a 4x Oversampling engine to prevent digital aliasing. Choose between Tube, Overdrive, or Fuzz wave-shaping algorithms.
* **Pitch / Octaver:** Polyphonic shifting utilizing a granular delay buffer and Triangle Windowing to prevent zipper noise.
* **Chorus / Flanger / Phaser / Tremolo:** Analog-modeled modulation. The Chorus pedal utilizes a custom IIR low-pass filter on the wet signal to mimic the warmth of vintage "Bucket Brigade" chips.
* **Auto-Wah:** An envelope-driven resonant bandpass filter. 
* **Delay / Reverb:** Time-based effects capable of everything from slapback to massive cathedral echoes.
* **Acoustic Sim:** Hollows out magnetic pickups and injects a 3ms micro-delay to simulate the resonance of a wooden body.
* **Guitar Synth:** AMDF-tracked monophonic synthesizer. Choose between Sine, Square, or Sawtooth oscillators.
* **Looper:** A 60-second tape machine. Features a custom state machine for seamless Stop -> Record -> Play -> Overdub transitions.
* **Bitcrusher / Ring Mod:** Destructive retro-hardware emulation for glitchy, lo-fi textures.
* **Cabinet (IR Loader):** A convolution engine that applies acoustic profiles to your raw signal. Use the built-in dropdown to select factory profiles, or click `LOAD .WAV IMPULSE` to import your own files.

---

## 📜 8. Credits & Open Source Acknowledgements
My Amp Sim is a passion project built on the shoulders of the audio engineering community. 

**Impulse Response Audio Assets**
The factory Cabinet Impulse Responses embedded within this software are derived from the exceptional **AC30BMS Impulse Response Pack**.
* **Creator:** km.202257 
* **Source / Documentation:** Included in the repository as `AC30BMS IR Pack Guide (km.202257).pdf`. 
* **Forum:** [Fractal Audio Systems Forum Profile](https://forum.fractalaudio.com/members/km-202257.62317/)

**C++ & DSP Framework**
Built utilizing the **JUCE** audio plugin framework, leveraging its core DSP module for FFT transforms, convolution, and oversampling protocols.
