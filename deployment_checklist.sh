#!/bin/bash
# 📋 MUTO-RS Deployment Checklist 
# Step-by-step guide for deploying the corrected repository
# Last Updated: Friday, April 16, 2026

set -e

REPO_ROOT="/home/edwin/MUTO-RS-CHOREGRAGPHY"
cd "$REPO_ROOT"

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║       MUTO-RS Deployment Checklist & Quick Start Guide        ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Helper functions
print_section() {
    echo ""
    echo "╔════════════════════════════════════════════════════════════╗"
    echo "║ $1"
    echo "╚════════════════════════════════════════════════════════════╝"
}

print_step() {
    echo "  [$1] $2"
}

print_input() {
    echo ""
    echo -e "${YELLOW}➜${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

# ─────────────────────────────────────────────────────────────────
# SECTION 1: Pre-Deployment Checks
# ─────────────────────────────────────────────────────────────────

print_section "STEP 1: Pre-Deployment Verification"

print_input "Checking repository completeness..."

required_files=(
    "README.md"
    "AUDIT_REPORT.md"
    "QUICK_REFERENCE.md"
    "CORRECTIONS_APPLIED.md"
    "docker/Dockerfile.muto_rs"
    "docker/Dockerfile.workstation"
    "docker/docker-compose.muto_rs.yml"
    "docker/docker-compose.workstation.yml"
    "docker/entrypoint.muto_rs.sh"
    "docker/entrypoint.workstation.sh"
    "Makefile"
    "make/common.mk"
    "make/docker.mk"
    "make/muto_rs.mk"
    "make/workstation.mk"
    "config/.env.muto_rs.example"
    "config/.env.workstation.example"
    "scripts/provision_muto_rs.sh"
    "scripts/provision_workstation.sh"
    "scripts/setup_dds.sh"
)

all_files_ok=true
for file in "${required_files[@]}"; do
    if [ -f "$file" ]; then
        print_step "✓" "$file"
    else
        print_error "$file"
        all_files_ok=false
    fi
done

if [ "$all_files_ok" = true ]; then
    print_success "All required files present"
else
    print_error "Some files missing - cannot proceed"
    exit 1
fi

# ─────────────────────────────────────────────────────────────────
# SECTION 2: System Requirements Check
# ─────────────────────────────────────────────────────────────────

print_section "STEP 2: System Requirements"

print_input "Checking Docker installation..."
if command -v docker &> /dev/null; then
    docker_version=$(docker --version)
    print_success "Docker installed: $docker_version"
else
    print_error "Docker not installed"
    print_input "Install Docker: https://docs.docker.com/install/"
    exit 1
fi

print_input "Checking Make installation..."
if command -v make &> /dev/null; then
    make_version=$(make --version | head -n1)
    print_success "Make installed: $make_version"
else
    print_error "Make not installed"
    print_input "Install with: sudo apt install build-essential"
    exit 1
fi

print_input "Checking SSH availability..."
if command -v ssh &> /dev/null; then
    print_success "SSH client available"
else
    print_error "SSH not available (needed for remote provisioning)"
fi

print_input "Checking Docker Compose..."
if docker compose version &> /dev/null || command -v docker-compose &> /dev/null; then
    print_success "Docker Compose available"
else
    print_error "Docker Compose not available"
    print_input "Install: https://docs.docker.com/compose/install/"
fi

# ─────────────────────────────────────────────────────────────────
# SECTION 3: Configuration Setup
# ─────────────────────────────────────────────────────────────────

print_section "STEP 3: Configuration Setup"

print_input "Creating local .env files from templates..."

if [ ! -f "config/.env.muto_rs" ]; then
    print_input "Robot .env file not found, creating from template..."
    cp config/.env.muto_rs.example config/.env.muto_rs
    print_success "Created config/.env.muto_rs (edit with your values)"
else
    print_success "config/.env.muto_rs already exists"
fi

if [ ! -f "config/.env.workstation" ]; then
    print_input "Workstation .env file not found, creating from template..."
    cp config/.env.workstation.example config/.env.workstation
    print_success "Created config/.env.workstation"
else
    print_success "config/.env.workstation already exists"
fi

print_input "Review and update configuration:"
print_step "→" "Edit config/.env.muto_rs with:"
print_step "  " "- ROBOT_LIST (CSV of robot hostnames)"
print_step "  " "- TS_AUTHKEY (Tailscale authentication key)"
print_step "  " "- TS_HOSTNAME (robot VPN name)"

print_step "→" "Edit config/.env.workstation with:"
print_step "  " "- Tailscale configuration (if needed)"
print_step "  " "- GPU_SUPPORT (true if NVIDIA GPU present)"

# ─────────────────────────────────────────────────────────────────
# SECTION 4: Quick Test
# ─────────────────────────────────────────────────────────────────

print_section "STEP 4: Quick Test - Build Workstation Image"

print_input "Testing build system with workstation image..."
print_step "→" "Building locally (this may take 5-10 minutes):"
print_step "  " "make build ENV=workstation PLATFORM=linux/amd64"
print_input "Press Enter to continue or Ctrl+C to skip..."
read -r

if make build ENV=workstation PLATFORM=linux/amd64 > /dev/null 2>&1; then
    print_success "Workstation image build successful"
else
    print_error "Build failed - check Docker and Makefile"
    exit 1
fi

# ─────────────────────────────────────────────────────────────────
# SECTION 5: Robot Provisioning
# ─────────────────────────────────────────────────────────────────

print_section "STEP 5: Robot Provisioning (Raspberry Pi 5)"

print_input "Setup robot with automated provisioning..."
print_step "→" "Get robot hostname/IP:"
print_step "  " "1. Connect Raspberry Pi to network"
print_step "  " "2. Find IP: sudo nmap -sn 192.168.1.0/24 | grep -i raspberry"
print_step "  " "3. Or check your router's device list"

print_input "Enter robot SSH connection (format: pi@192.168.1.100):"
read -r robot_ssh

if [ -z "$robot_ssh" ]; then
    print_error "No robot address provided - skipping provisioning"
else
    print_input "Starting robot provisioning (will take 10-15 minutes)..."
    print_step "→" "Running: make provision-muto-rs SSH_HOST=$robot_ssh"
    
    if make provision-muto-rs SSH_HOST="$robot_ssh"; then
        print_success "Robot provisioning complete"
    else
        print_error "Provisioning failed - check SSH connection and logs"
    fi
fi

# ─────────────────────────────────────────────────────────────────
# SECTION 6: Deploy to Robot
# ─────────────────────────────────────────────────────────────────

print_section "STEP 6: Build & Deploy to Robot"

print_input "Building production image for Raspberry Pi 5 (ARM64)..."
print_step "→" "This requires buildx and may take 15+ minutes"
print_input "Continue? (y/n)"
read -r continue_build

if [ "$continue_build" = "y" ]; then
    if make build ENV=muto-rs PLATFORM=linux/arm64; then
        print_success "ARM64 build complete"
        
        if [ -n "$robot_ssh" ]; then
            print_input "Transfer image to robot..."
            make send-image SSH_HOST="$robot_ssh" ENV=muto-rs
            
            print_input "Deploy on robot..."
            make muto-rs-deploy SSH_HOST="$robot_ssh"
        fi
    fi
fi

# ─────────────────────────────────────────────────────────────────
# SECTION 7: Verification
# ─────────────────────────────────────────────────────────────────

print_section "STEP 7: Verification & Testing"

print_input "Verify deployed services..."
if [ -n "$robot_ssh" ]; then
    print_step "→" "Checking robot status: make muto-rs-status SSH_HOST=$robot_ssh"
    print_step "→" "View logs: make muto-rs-logs SSH_HOST=$robot_ssh"
fi

print_step "→" "Test local workstation: docker compose -f docker/docker-compose.workstation.yml up"

# ─────────────────────────────────────────────────────────────────
# SECTION 8: Quick Reference
# ─────────────────────────────────────────────────────────────────

print_section "Common Commands Reference"

echo ""
echo "🏗️  BUILD OPERATIONS"
print_step "→" "make help                    # Show all available targets"
print_step "→" "make build ENV=workstation   # Build development image"
print_step "→" "make build ENV=muto-rs       # Build production image (ARM64)"

echo ""
echo "🚀 DEPLOYMENT"
print_step "→" "make provision-workstation   # Setup local workstation"
print_step "→" "make workstation-deploy      # Start workstation containers"
print_step "→" "make muto-rs-deploy SSH_HOST=pi@robot.local  # Deploy on robot"

echo ""
echo "📊 MONITORING"
print_step "→" "make workstation-logs        # View workstation container logs"
print_step "→" "make workstation-status      # Show running containers"
print_step "→" "make muto-rs-logs SSH_HOST=pi@robot.local   # Robot logs"
print_step "→" "make muto-rs-status SSH_HOST=pi@robot.local # Robot status"

echo ""
echo "🧹 CLEANUP"
print_step "→" "make workstation-stop        # Stop workstation containers"
print_step "→" "make muto-rs-stop SSH_HOST=pi@robot.local   # Stop robot"

# ─────────────────────────────────────────────────────────────────
# FINAL SUMMARY
# ─────────────────────────────────────────────────────────────────

print_section "✅ DEPLOYMENT CHECKLIST COMPLETE"

echo ""
echo "📚 DOCUMENTATION:"
print_step "→" "README.md               - Project overview and architecture"
print_step "→" "AUDIT_REPORT.md         - Detailed audit findings"
print_step "→" "QUICK_REFERENCE.md      - Command reference and workflows"
print_step "→" "CORRECTIONS_APPLIED.md  - Summary of all corrections"

echo ""
echo "🎯 NEXT STEPS:"
print_step "1" "Review configuration files (config/.env.*)"
print_step "2" "Test build locally: make build ENV=workstation"
print_step "3" "Provision workstation: make provision-workstation"
print_step "4" "Get robot IP and provision: make provision-muto-rs SSH_HOST=..."
print_step "5" "Build and deploy: make build ENV=muto-rs && make send-image ..."

echo ""
echo "📞 TROUBLESHOOTING:"
print_step "→" "Docker issues? See: docker info"
print_step "→" "Build failures? Check: make help"
print_step "→" "SSH connection issues? Verify: ssh -v pi@robot.local"
print_step "→" "More help? See QUICK_REFERENCE.md"

echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║         🎉 Repository is ready for deployment! 🎉            ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""
