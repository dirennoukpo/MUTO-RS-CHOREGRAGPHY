#!/bin/bash
##
## setup_dds.sh - FastDDS configuration for MUTO-RS multi-robot communication
##
## This script configures ROS2 DDS middleware for multi-robot discovery.
## Supports:
##   - Local network discovery (default: Simple Discovery Protocol)
##   - VPN discovery (optional: Discovery Server mode)
##
## Usage:
##   bash setup_dds.sh [--server|--client|--simple]
##   Default: simple (local network multicast discovery)

set -e

DDS_MODE="${1:-simple}"
DDS_CONFIG_DIR="${XDG_CONFIG_HOME:=$HOME/.ros}"
FASTDDS_CONFIG="$DDS_CONFIG_DIR/dds_config.xml"

echo "🔧 Setting up FastDDS configuration..."
echo "   Mode: $DDS_MODE"
echo "   Config dir: $DDS_CONFIG_DIR"

mkdir -p "$DDS_CONFIG_DIR"

case "$DDS_MODE" in
    simple)
        echo "✓ Using Simple Discovery Protocol (local network multicast)"
        cat > "$FASTDDS_CONFIG" <<'EOF'
<?xml version="1.0" encoding="UTF-8" ?>
<dds>
    <profiles xmlns="http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles">
        <participant profile_name="default_participant" is_default_profile="true">
            <rtps>
                <builtin>
                    <discovery_config>
                        <discoveryProtocol>SIMPLE</discoveryProtocol>
                    </discovery_config>
                </builtin>
            </rtps>
        </participant>
    </profiles>
</dds>
EOF
        ;;
    
    server)
        echo "✓ Using Discovery Server mode (central discovery server)"
        cat > "$FASTDDS_CONFIG" <<'EOF'
<?xml version="1.0" encoding="UTF-8" ?>
<dds>
    <profiles xmlns="http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles">
        <participant profile_name="default_participant" is_default_profile="true">
            <rtps>
                <builtin>
                    <discovery_config>
                        <discoveryProtocol>SERVER</discoveryProtocol>
                        <discoveryServersList>
                            <RemoteServer prefix="44.53.00.5f.49.50.2e.31">
                                <metatrafficUnicastLocatorList>
                                    <!-- localhost:11811 -->
                                    <locator>
                                        <udpv4>
                                            <address>127.0.0.1</address>
                                            <port>11811</port>
                                        </udpv4>
                                    </locator>
                                </metatrafficUnicastLocatorList>
                            </RemoteServer>
                        </discoveryServersList>
                    </discovery_config>
                </builtin>
            </rtps>
        </participant>
    </profiles>
</dds>
EOF
        ;;
    
    client)
        echo "✓ Using Discovery Server Client mode (connects to discovery server)"
        cat > "$FASTDDS_CONFIG" <<'EOF'
<?xml version="1.0" encoding="UTF-8" ?>
<dds>
    <profiles xmlns="http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles">
        <participant profile_name="default_participant" is_default_profile="true">
            <rtps>
                <builtin>
                    <discovery_config>
                        <discoveryProtocol>CLIENT</discoveryProtocol>
                        <discoveryServersList>
                            <RemoteServer prefix="44.53.00.5f.49.50.2e.31">
                                <metatrafficUnicastLocatorList>
                                    <!-- Update this to point to your discovery server -->
                                    <locator>
                                        <udpv4>
                                            <address>discovery-server-ip</address>
                                            <port>11811</port>
                                        </udpv4>
                                    </locator>
                                </metatrafficUnicastLocatorList>
                            </RemoteServer>
                        </discoveryServersList>
                    </discovery_config>
                </builtin>
            </rtps>
        </participant>
    </profiles>
</dds>
EOF
        ;;
    
    *)
        echo "❌ Unknown mode: $DDS_MODE"
        echo "   Usage: $0 [--server|--client|--simple]"
        exit 1
        ;;
esac

echo "✅ FastDDS configured: $FASTDDS_CONFIG"
echo ""
echo "📝 Next steps:"
echo "   1. Set environment variable: export ROS_DDS_CONFIG_FILE=$FASTDDS_CONFIG"
echo "   2. Or add to ~/.bashrc: echo 'export ROS_DDS_CONFIG_FILE=$FASTDDS_CONFIG' >> ~/.bashrc"
echo "   3. Launch ROS2 nodes normally"

