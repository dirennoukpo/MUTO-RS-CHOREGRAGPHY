# MUTO-RS Choreography

**Multi-robot choreography orchestration via ROS2 for hexapod robots.**

## 🤖 Overview

MUTO-RS is a project that orchestrates synchronized dance choreography across multiple hexapod robots using ROS2 for real-time communication and coordination.

### Key Features

- **Multi-Robot Orchestration**: Leader-follower architecture for synchronized robot movement
- **ROS2 Humble Integration**: Distributed computing via ROS2 DDS
- **Audio-Driven Choreography**: Beat-detection and dance synchronization to music
- **Tailscale VPN Support**: Secure multi-robot networking over VPN
- **Docker Deployments**: Production-ready images for robots and workstations

## 📁 Project Structure

```
MUTO-RS-CHOREOGRAPHY/
├── docker/                    # Docker images & deployment configuration
│   ├── Dockerfile.muto_rs    # Production image for Raspberry Pi 5
│   ├── Dockerfile.workstation # Dev image for local development
│   ├── docker-compose.*.yml   # Docker Compose deployment configs
│   └── entrypoint.*.sh        # Container startup scripts
├── muto_ws/                   # ROS2 Workspace (colcon)
│   └── src/muto_rs_synchronization/
│       ├── dance_leader.py    # Orchestrator node
│       ├── dance_follower.py  # Robot follower node  
│       └── launch/            # ROS2 launch files
├── make/                      # Build & deployment Makefiles
│   ├── common.mk             # Shared variables & functions
│   ├── docker.mk             # Docker build targets
│   ├── muto_rs.mk            # Robot provisioning & deployment
│   └── workstation.mk        # Workstation development targets
├── scripts/                   # Provisioning & setup scripts
│   ├── provision_muto_rs.sh
│   ├── provision_workstation.sh
│   └── setup_dds.sh
└── config/                    # Configuration files
    ├── dds_config.xml        # ROS2 DDS middleware config
    └── .env.*                # Environment variables (local)
```

## 🚀 Quick Start

### For Robot (Raspberry Pi 5):

```bash
# 1. Provision the robot
ssh pi@robot "bash provision_muto_rs.sh"

# 2. Build Docker image locally
make build ENV=muto-rs PLATFORM=linux/arm64

# 3. Send image to robot
make send-image SSH_HOST=pi@robot-ip ENV=muto-rs

# 4. Deploy on robot
ssh pi@robot "cd ~/muto_rs && docker compose -f docker/docker-compose.muto_rs.yml up -d"
```

### For Workstation (Local Development):

```bash
# 1. Provision local environment
bash scripts/provision_workstation.sh

# 2. Build development image
make build ENV=workstation

# 3. Deploy locally
make workstation-deploy

# 4. View logs
make workstation-logs
```

## 🔧 Configuration

### Robot (.env.muto_rs)

```bash
PLATFORM=linux/arm64
ROS_DISTRO=humble
ROS_DOMAIN_ID=0
MUTO_RS_IMAGE=muto-rs-robot
TS_AUTHKEY=<your-tailscale-key>
TS_HOSTNAME=muto-rs-robot-01
```

### Workstation (.env.workstation)

```bash
PLATFORM=linux/amd64
ROS_DISTRO=humble
MUTO_WS_IMAGE=muto-workstation-dev
VOLUME_MUTO_WS=/path/to/muto_ws
```

## 📋 Makefile Targets

### Common

```bash
make help              # Show all available targets
make check-docker      # Verify Docker installation
```

### Docker Build & Deploy

```bash
make build ENV=muto-rs PLATFORM=linux/arm64          # Build for robot
make build ENV=workstation PLATFORM=linux/amd64     # Build for workstation
make send-image SSH_HOST=pi@robot ENV=muto-rs       # Send image to robot
```

### Robot Deployment

```bash
make provision-muto-rs SSH_HOST=pi@robot-ip         # Provision robot
make muto-rs-deploy SSH_HOST=pi@robot               # Deploy stack
make muto-rs-logs SSH_HOST=pi@robot                 # View logs
make muto-rs-status SSH_HOST=pi@robot               # Check status
```

### Workstation Development

```bash
make provision-workstation          # Setup local dev environment
make workstation-build             # Build workstation image
make workstation-deploy            # Deploy locally
make workstation-logs              # View container logs
make workstation-status            # Check stack status
make workstation-shell             # Open interactive shell
```

## 🌐 ROS2 DDS Configuration

### Local Network Discovery (Default)

```bash
bash scripts/setup_dds.sh simple
```

### VPN/Remote Discovery (Tailscale)

```bash
bash scripts/setup_dds.sh server    # Central discovery server
bash scripts/setup_dds.sh client    # Client connects to server
```

## 🏗️ Docker Images

### muto-rs-robot (Production)

- **Base**: ubuntu:22.04
- **Architecture**: ARM64 (Raspberry Pi 5)
- **Size**: ~500MB
- **Contents**: ROS2, GPIO/I2C drivers, audio libs, MutoLib
- **Entrypoint**: Strict production (fails if workspace not built)

### muto-workstation-dev (Development)

- **Base**: ros:humble-ros-base-jammy
- **Architecture**: AMD64 (Local machine)
- **Size**: ~2.5GB (includes dev tools, RViz2, Nav2)
- **Contents**: All muto-rs + dev tools, simulation, ML libraries
- **Entrypoint**: Permissive dev (auto-builds workspace if needed)

## 🔐 Security

- Non-root user "muto" in all containers
- Hardware access via device mount restrictions
- Tailscale VPN for encrypted multi-robot communication
- .env files excluded from git (.gitignore)

## 📝 Development

### Build Workspace Locally

```bash
cd muto_ws
colcon build --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
```

### Run ROS2 Nodes

```bash
# Leader (orchestrator)
ros2 run muto_rs_synchronization dance_leader.py --loops 1 --beat 0.9

# Follower (robot)
ros2 run muto_rs_synchronization dance_follower.py --step-width 18 --dry-run

# Using launch file
ros2 launch muto_rs_synchronization dance_choreography.launch.py mode:=leader
```

## 📚 Documentation

- **DDS Configuration**: `config/dds_config.xml`
- **Package Info**: `muto_ws/src/muto_rs_synchronization/package.xml`
- **Launch File**: `muto_ws/src/muto_rs_synchronization/launch/dance_choreography.launch.py`
- **Build System**: `make/common.mk`, `make/docker.mk`

## 🐛 Troubleshooting

### Docker Build Fails

```bash
# Clear cache and rebuild
make build ENV=workstation CACHE=0

# Check for syntax errors
docker compose config -f docker/docker-compose.workstation.yml
```

### ROS2 Discovery Issues

```bash
# Check DDS configuration
export ROS_DDS_CONFIG_FILE=$HOME/.ros/dds_config.xml
ros2 node list  # Should discover all nodes
```

### Container Won't Start

```bash
# Check logs
docker logs muto-workstation-dev

# Verify environment
docker compose --env-file config/.env.workstation config
```

## 🤝 Contributing

1. Create feature branch: `git checkout -b feature/my-feature`
2. Commit changes: `git commit -am 'Add feature'`
3. Push to branch: `git push origin feature/my-feature`
4. Open a Pull Request

## 📄 License

MIT License - See LICENSE file

## 👥 Authors

- **Diren Noukpo** <diren.noukpo@epitech.eu>
- MUTO-RS Contributors

---

**Last Updated**: Fri Apr 16 16:45:00 PM 2026
