# Parallel Collision Lab

A 3D real-time physics simulation, collision detection, and parallel computing laboratory built with C++20 and DirectX 11.

## Directory

```text
ParallelCollisionLab/
├── Collision/     # Broad-phase & narrow-phase collision solvers (Strategy Pattern)
├── Core/          # Vector math, physics primitives (Sphere), CPU info, and controller
├── Network/       # Winsock2 non-blocking UDP networking (Server, Client, Protocol, Facade)
├── Renderer/      # DirectX 11 rendering pipeline, debug visualizers, and DirectWrite HUD
└── Window/        # Win32 window management, benchmark dialog window, and input handling
```

## Features

### Collision Solvers
Implements 6 collision detection solvers comparing algorithmic complexity ($O(N^2)$, $O(N)$, $O(N \log N)$)

configurations:
- **Nested Loop (ST / MT)**: Brute-force baseline ($O(N^2)$).
- **Uniform Grid (ST / MT)**: Spatial hashing for uniform distributions ($O(N)$).
- **BVH (ST / MT)**: AABB tree spatial hierarchy for non-uniform distributions ($O(N \log N)$).

### Renderer (DirectX 11)
- DirectX 11 rendering pipeline with custom HLSL vertex & pixel shaders.
- Real-time debug visualizers for Uniform Grid cells and BVH AABB bounding boxes.
- Direct2D / DirectWrite real-time profiling HUD.

### Networking (Winsock2 UDP)
- Lightweight non-blocking UDP architecture (`ioctlsocket(FIONBIO)`).

---

## 프로젝트 개요 (Korean Overview)

**Parallel Collision Lab**은 C++20과 DirectX 11을 기반으로 구축된 3D 실시간 물리 시뮬레이션 및 병렬 컴퓨팅 벤치마크 실험실입니다.

수천 개의 구체들이 실시간으로 상호작용하는 환경에서 대표적인 충돌 감지 알고리즘을 단일 스레드 및 OpenMP 멀티스레드 환경에서 비교 분석할 수 있습니다.
