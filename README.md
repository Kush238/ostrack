# OS Track

**OS Track** is a cross-platform application for tracking and interacting with Operating System applications, written in **C**.

 **OS Track** supports **Windows 8/10/11**, **Linux** (X11) and **macOS**.

---

## Features:

- Tracks open application windows.
- Can retrieve window titles and other metadata.
- Designed for cross-platform compilation.

---

## Requirements:

### Windows
- **MinGW-w64** (GCC)  
  Download: [https://www.mingw-w64.org/](https://www.mingw-w64.org/)
- **VS Code C/C++ Extension** by Microsoft.

### Linux
- GCC
- X11 Development Libraries:

```bash
sudo apt-get install libx11-dev	
```

### macOS

**Xcode** Command Line Tools:

```bash
xcode-select --install
```

X11 Support via **XQuartz**:

```bash
brew install xquartz
```

---
## Compiling and Executing:

### Windows

Compile:
```bash
gcc main.c -o main.exe
```

Execute:
```bash
.\main.exe
```

### Linux

Compile:
```bash
gcc main.c -o main -lX11
```

Execute:
```bash
./main
```

### macOS

Compile:
```bash
gcc main.c -o main -I/opt/X11/include -L/opt/X11/lib -lX11
```

Execute:
```bash
./main
```


