# ⚡ MUTO-RS Quick Reference

**Fast lookup guide for common tasks and commands**

## 🎯 Most Common Commands

```bash
# Build workstation image
make build ENV=workstation

# Deploy workstation locally
make workstation-deploy

# Check deployment status
make workstation-status

# View logs
make workstation-logs

# Stop everything
make workstation-stop
```

## 🤖 Robot Deployment (Raspberry Pi 5)

```bash
# Provision robot (run once)
make provision-muto-rs SSH_HOST=pi@192.168.1.161

# Build ARM64 image locally
make build ENV=muto-rs PLATFORM=linux/arm64

# Send image to robot
make send-image SSH_HOST=pi@192.168.1.161 ENV=muto-rs

# Deploy on robot
make muto-rs-deploy SSH_HOST=pi@192.168.1.161

# Check robot logs
make muto-rs-logs SSH_HOST=pi@192.168.1.161
```

## 📋 All Make Targets

### Info & Help
```bash
make help              # Show all targets
make check-docker      # Verify Docker
make check-ssh         # Verify SSH connection
```

### Build (Docker Images)
```bash
make build ENV=muto-rs PLATFORM=linux/arm64        # Build for robot
make build ENV=workstation PLATFORM=linux/amd64   # Build for dev
make build ENV=workstation CACHE=0                 # Skip cache
```

### Send Images
```bash
make send-image SSH_HOST=user@host ENV=muto-rs    # To robot
make send-image SSH_HOST=localhost ENV=workstation # Local test
```

### Robot (muto_rs) Targets
```bash
make provision-muto-rs SSH_HOST=pi@robot      # First-time setup
make muto-rs-deploy SSH_HOST=pi@robot         # Start services
make muto-rs-stop SSH_HOST=pi@robot           # Stop services
make muto-rs-logs SSH_HOST=pi@robot           # View logs
make muto-rs-status SSH_HOST=pi@robot         # Check status
```

### Workstation Targets
```bash
make provision-workstation          # Local dev setup
make workstation-build             # Build local image
make workstation-deploy            # Start services locally
make workstation-stop              # Stop services
make workstation-logs              # View logs
make workstation-status            # Check stack status
make workstation-shell             # Interactive shell
make workstation-rviz              # Launch RViz2
```

## 🔧 Configuration Files

| File | Purpose | Edit When |
|------|---------|-----------|
| `config/.env.muto_rs` | Robot config | Need different hostname or Tailscale key |
| `config/.env.workstation` | Dev config | Local volume path changes |
| `config/dds_config.xml` | ROS2 DDS | Changing discovery method |

## 🐳 Docker Compose Commands

```bash
# Validate config
docker compose -f docker/docker-compose.workstation.yml config

# Deploy manually
docker compose --env-file config/.env.workstation \
  -f docker/docker-compose.workstation.yml up -d

# Stop manually
docker compose -f docker/docker-compose.workstation.yml down

# View logs
docker compose -f docker/docker-compose.workstation.yml logs -f
```

## 📋 Environment Variables

### Robot (.env.muto_rs)
```bash
PLATFORM=linux/arm64
ROS_DISTRO=humble
MUTO_RS_IMAGE=muto-rs-robot
TS_AUTHKEY=<your-key>
TS_HOSTNAME=muto-rs-robot-01
```

### Workstation (.env.workstation)
```bash
PLATFORM=linux/amd64
ROS_DISTRO=humble
MUTO_WS_IMAGE=muto-workstation-dev
VOLUME_MUTO_WS=/path/to/muto_ws
```

## 🚀 ROS2 Commands

```bash
# Inside container, launch leader
ros2 launch muto_rs_synchronization dance_choreography.launch.py mode:=leader

# Inside container, launch follower
ros2 launch muto_rs_synchronization dance_choreography.launch.py mode:=follower

# List all nodes
ros2 node list

# List all topics
ros2 topic list

# Show topic message
ros2 topic echo /dance_cmd
```

## 🌐 DDS Configuration

```bash
# Simple (local network, default)
bash scripts/setup_dds.sh simple

# Discovery Server (VPN/remote)
bash scripts/setup_dds.sh server

# Discovery Client
bash scripts/setup_dds.sh client
```

## 🔍 Docker Image Info

```bash
# List images
docker images | grep muto

# Inspect image
docker image inspect muto-workstation-dev:latest

# Save image
docker save muto-rs-robot > muto-rs-robot.tar

# Load image
docker load < muto-rs-robot.tar
```

## 🛠️ Debugging

### Check Docker build
```bash
make build ENV=workstation 2>&1 | tee build.log
```

### Check colcon build in image
```bash
docker run -it muto-workstation-dev:latest bash
cd /muto_ws
colcon build --symlink-install
```

### Check DDS discovery
```bash
docker compose exec muto-workstation bash
ros2 node list  # Should show nodes across network
```

### SSH into robot
```bash
ssh pi@robot
docker ps              # See running containers
docker logs -f muto-rs # Follow logs
```

## 🐛 Quick Troubleshooting

| Problem | Solution |
|---------|----------|
| `make: *** missing separator` | Makefile TAB issue. Run `bash fix_repository.sh` |
| Workspace not built | Docker build failed. Check build logs |
| ROS2 DDS not discovering nodes | Run `bash scripts/setup_dds.sh simple` |
| SSH_HOST errors | Set variable: `export SSH_HOST=user@host` |
| Docker permission denied | Add user to group: `sudo usermod -aG docker $USER` |

## 📚 Important Files

| File | Purpose |
|------|---------|
| `Dockerfile.muto_rs` | Production robot image |
| `Dockerfile.workstation` | Dev workstation image |
| `docker-compose.*.yml` | Container orchestration |
| `entrypoint.*.sh` | Container startup logic |
| `make/docker.mk` | Build targets |
| `make/muto_rs.mk` | Robot deployment targets |
| `scripts/provision_*.sh` | One-time setup scripts |
| `scripts/setup_dds.sh` | ROS2 DDS configuration |

## 💾 Project Directories

```
~/muto_rs                   # Robot deployment dir (on Raspberry Pi)
~/muto_rs/config            # Config files (.env.muto_rs, dds_config.xml)
~/muto_rs_data              # Persistent data (logs, rosbags)
~/muto_rs_workstation       # Workstation deployment dir (local machine)
~/muto_rs_workstation/data  # Dev data (logs, datasets)
```

## 📞 Get Help

- **Full documentation**: `cat README.md`
- **Audit report**: `cat AUDIT_REPORT.md`
- **All targets**: `make help`
- **Make targets help**: `make help-all`

## ⚡ One-Liner Deployments

```bash
# Full workstation setup
bash provision_workstation.sh && make build ENV=workstation && make workstation-deploy

# Full robot provisioning + deployment
make provision-muto-rs SSH_HOST=pi@robot && make build ENV=muto-rs PLATFORM=linux/arm64 && make send-image SSH_HOST=pi@robot ENV=muto-rs && make muto-rs-deploy SSH_HOST=pi@robot

# Quick status check
make workstation-status && echo "---" && make muto-rs-status SSH_HOST=pi@robot
```

---

**Last Updated**: Fri Apr 16 16:45:00 PM 2026  
**Quick Reference v1.0**
