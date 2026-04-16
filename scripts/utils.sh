##
## utils.sh for MUTO-RS-CHOREGRAGPHY [WSL: Ubuntu] in /home/edwin/MUTO-RS-CHOREGRAGPHY/scripts
##
## Made by dirennoukpo
## Login   <diren.noukpo@epitech.eu>
##
## Started on  Thu Apr 16 11:21:45 AM 2026 dirennoukpo
## Last update Fri Apr 16 6:33:18 PM 2026 dirennoukpo
##

set -euo pipefail

log() {
	echo "[$(date +'%Y-%m-%d %H:%M:%S')] $*"
}

warn() {
	echo "⚠️  $*"
}

die() {
	echo "❌ $*" >&2
	exit 1
}

ensure_not_root() {
	if [ "${EUID:-$(id -u)}" -eq 0 ]; then
		die "Do not run this script as root. Use a normal user with sudo available."
	fi
}

ensure_internet() {
	if ! timeout 3 ping -c 1 8.8.8.8 >/dev/null 2>&1; then
		die "No internet connection detected."
	fi
}

load_env_file() {
	local env_file="$1"
	if [ -f "$env_file" ]; then
		set -a
		# shellcheck disable=SC1090
		. "$env_file"
		set +a
		log "Loaded environment file: $env_file"
	else
		warn "Environment file not found: $env_file"
	fi
}

install_docker_official_repo() {
	local os_id distro codename
	os_id="$(. /etc/os-release && echo "${ID:-unknown}")"
	case "$os_id" in
		debian|raspbian)
			distro="debian"
			;;
		ubuntu)
			distro="ubuntu"
			;;
		*)
			distro="debian"
			warn "Unrecognized distro '$os_id'; defaulting Docker repo to Debian"
			;;
	esac
	codename="$(. /etc/os-release && echo "${VERSION_CODENAME:-${UBUNTU_CODENAME:-}}")"
	[ -n "$codename" ] || die "Unable to detect OS codename for Docker repository setup"

	log "Installing Docker from the official ${distro} repository"
	sudo apt-get update
	sudo apt-get remove -y docker.io docker-doc docker-compose docker-compose-v2 podman-docker containerd runc >/dev/null 2>&1 || true
	sudo apt-get install -y ca-certificates curl
	sudo install -m 0755 -d /etc/apt/keyrings
	sudo curl -fsSL "https://download.docker.com/linux/${distro}/gpg" -o /etc/apt/keyrings/docker.asc
	sudo chmod a+r /etc/apt/keyrings/docker.asc
	echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/${distro} ${codename} stable" | sudo tee /etc/apt/sources.list.d/docker.list >/dev/null
	sudo apt-get update
	sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
	sudo systemctl enable --now docker
}

install_docker_official_ubuntu() {
	install_docker_official_repo
}

