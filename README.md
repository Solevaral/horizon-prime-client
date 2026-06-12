# Horizon Prime

A modern terminal-based game client featuring real-time rendering with OpenGL and custom protocol support.

**[Read in Russian / Читать по-русски](README.ru.md)**

## Overview

Horizon Prime is a sophisticated terminal client that connects to a remote server for an immersive gaming experience. The client leverages cutting-edge graphics techniques to deliver smooth animations and visual effects directly in your terminal.

### Features

- **Real-time Rendering**: Powered by OpenGL for hardware-accelerated graphics
- **Custom Protocol**: Efficient binary communication with the server
- **Text-based UI**: Advanced text rendering with TrueType font support (stb_truetype)
- **Scene Management**: Dynamic scene and animation system
- **Cross-platform**: Built with C++ for maximum compatibility

## Building

### Requirements

- C++17 or later
- OpenGL 3.3+
- CMake 3.15+

\\\ash
git clone https://github.com/yourusername/horizon-prime-client.git
cd horizon-prime-client
mkdir build && cd build
cmake ..
make
\\\

## Running

\\\ash
./horizon-prime-client --server localhost:9000
\\\

## Architecture

The client uses a modular architecture:

- **Renderer**: OpenGL-based graphics pipeline
- **Protocol**: Custom binary format for server communication
- **Input**: Terminal input handling
- **Scene Graph**: Hierarchical scene representation

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

[Add your license here]

## Author

Solevaral
