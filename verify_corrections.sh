#!/bin/bash
# ✅ MUTO-RS Repository Correction Checklist
# 
# Run this checklist to verify all corrections have been properly applied.
# Fri Apr 16 16:45:00 PM 2026

set -e

REPO_ROOT="/home/edwin/MUTO-RS-CHOREGRAGPHY"
cd "$REPO_ROOT"

echo "═══════════════════════════════════════════════════════════════"
echo "✅ MUTO-RS REPOSITORY CORRECTION VERIFICATION CHECKLIST"
echo "═══════════════════════════════════════════════════════════════"
echo ""

PASS=0
FAIL=0
WARN=0

# Helper functions
check_pass() {
    echo "✅ PASS: $1"
    ((PASS++))
}

check_fail() {
    echo "❌ FAIL: $1"
    ((FAIL++))
}

check_warn() {
    echo "⚠️  WARN: $1"
    ((WARN++))
}

# ─────────────────────────────────────────────────────────────────
# SECTION 1: Documentation Files
# ─────────────────────────────────────────────────────────────────

echo "📋 SECTION 1: Documentation Files"
echo "─────────────────────────────────────────────────────────────"

if [ -s README.md ]; then
    check_pass "README.md exists and not empty ($(wc -l < README.md) lines)"
else
    check_fail "README.md missing or empty"
fi

if [ -s AUDIT_REPORT.md ]; then
    check_pass "AUDIT_REPORT.md exists ($(wc -l < AUDIT_REPORT.md) lines)"
else
    check_fail "AUDIT_REPORT.md missing"
fi

if [ -s QUICK_REFERENCE.md ]; then
    check_pass "QUICK_REFERENCE.md exists ($(wc -l < QUICK_REFERENCE.md) lines)"
else
    check_fail "QUICK_REFERENCE.md missing"
fi

echo ""

# ─────────────────────────────────────────────────────────────────
# SECTION 2: Configuration Files - No MECAMATE References
# ─────────────────────────────────────────────────────────────────

echo "🔍 SECTION 2: Configuration - MECAMATE References"
echo "─────────────────────────────────────────────────────────────"

for file in config/.env.muto_rs config/.env.workstation config/dds_config.xml; do
    if grep -q -i "mecamate" "$file" 2>/dev/null; then
        check_fail "Found MECAMATE reference in $file"
    else
        check_pass "No MECAMATE references in $file"
    fi
done

echo ""

# ─────────────────────────────────────────────────────────────────
# SECTION 3: Docker Files Completeness
# ─────────────────────────────────────────────────────────────────

echo "🐳 SECTION 3: Docker Files"
echo "─────────────────────────────────────────────────────────────"

for file in docker/Dockerfile.muto_rs docker/Dockerfile.workstation \
            docker/docker-compose.muto_rs.yml docker/docker-compose.workstation.yml; do
    if [ -s "$file" ]; then
        check_pass "$file exists ($(wc -l < "$file") lines)"
    else
        check_fail "$file missing or empty"
    fi
done

echo ""

# ─────────────────────────────────────────────────────────────────
# SECTION 4: Entrypoint Script Checks
# ─────────────────────────────────────────────────────────────────

echo "🚀 SECTION 4: Entrypoint Scripts"
echo "─────────────────────────────────────────────────────────────"

for file in docker/entrypoint.muto_rs.sh docker/entrypoint.workstation.sh; do
    if [ -f "$file" ]; then
        shebang_count=$(grep -c "^#!/bin/bash" "$file" || true)
        if [ "$shebang_count" -eq 1 ]; then
            check_pass "$file: Single shebang line"
        elif [ "$shebang_count" -gt 1 ]; then
            check_fail "$file: Multiple shebang lines ($shebang_count found)"
        fi
        
        if grep -q "set -eo pipefail" "$file"; then
            check_pass "$file: Has error handling"
        else
            check_fail "$file: Missing error handling (set -eo pipefail)"
        fi
    fi
done

echo ""

# ─────────────────────────────────────────────────────────────────
# SECTION 5: Makefile Analysis
# ─────────────────────────────────────────────────────────────────

echo "📝 SECTION 5: Makefile Files"
echo "─────────────────────────────────────────────────────────────"

# Check if Makefile includes common.mk
if grep -q "include make/common.mk" Makefile; then
    check_pass "Root Makefile includes common.mk"
else
    check_fail "Root Makefile does not include common.mk"
fi

# Check for unsafe includes
unsafe_includes=$(grep -r "^include config/" make/*.mk 2>/dev/null || true | wc -l)
if [ "$unsafe_includes" -eq 0 ]; then
    check_pass "No unsafe 'include' statements (all use '-include')"
else
    check_warn "$unsafe_includes unsafe 'include' statements found (should use '-include')"
fi

# Check muto_rs.mk for implemented targets
for target in "provision-muto-rs:" "muto-rs-deploy:" "muto-rs-stop:" "muto-rs-logs:" "muto-rs-status:"; do
    if grep -q "^$target" make/muto_rs.mk; then
        check_pass "make/muto_rs.mk: $target defined"
    else
        check_fail "make/muto_rs.mk: $target missing"
    fi
done

# Check workstation.mk for implemented targets
for target in "workstation-deploy:" "workstation-stop:" "workstation-logs:" "workstation-status:"; do
    if grep -q "^$target" make/workstation.mk; then
        check_pass "make/workstation.mk: $target defined"
    else
        check_fail "make/workstation.mk: $target missing"
    fi
done

echo ""

# ─────────────────────────────────────────────────────────────────
# SECTION 6: Scripts Completeness
# ─────────────────────────────────────────────────────────────────

echo "📦 SECTION 6: Scripts"
echo "─────────────────────────────────────────────────────────────"

for script in scripts/provision_muto_rs.sh scripts/provision_workstation.sh scripts/setup_dds.sh; do
    if [ -f "$script" ]; then
        if [ -s "$script" ]; then
            lines=$(wc -l < "$script")
            check_pass "$script exists ($lines lines)"
        else
            check_fail "$script exists but is empty"
        fi
        
        if grep -q "#!/bin/bash" "$script"; then
            check_pass "$script: Valid bash script"
        else
            check_fail "$script: Missing shebang"
        fi
    else
        check_fail "$script missing"
    fi
done

# Check fix_repository.sh
if [ -f "fix_repository.sh" ]; then
    check_pass "fix_repository.sh exists (auto-fix script available)"
else
    check_warn "fix_repository.sh missing (run to apply remaining fixes)"
fi

echo ""

# ─────────────────────────────────────────────────────────────────
# SECTION 7: Path Consistency
# ─────────────────────────────────────────────────────────────────

echo "📂 SECTION 7: Path Consistency"
echo "─────────────────────────────────────────────────────────────"

# Check for old paths
old_paths=$(grep -r "mecamate_logs\|mecamate_rosbag" . --exclude-dir=.git 2>/dev/null || true | wc -l)
if [ "$old_paths" -eq 0 ]; then
    check_pass "No obsolete paths (mecamate_logs, mecamate_rosbag)"
else
    check_fail "Found $old_paths references to obsolete paths"
fi

# Check for muto_rs_data usage
if grep -q "muto_rs_data" docker/docker-compose.muto_rs.yml; then
    check_pass "docker-compose.muto_rs.yml uses muto_rs_data"
else
    check_warn "docker-compose.muto_rs.yml might not reference muto_rs_data"
fi

echo ""

# ─────────────────────────────────────────────────────────────────
# SECTION 8: Environment Variables
# ─────────────────────────────────────────────────────────────────

echo "🔑 SECTION 8: Environment Variables"
echo "─────────────────────────────────────────────────────────────"

required_muto_rs_vars=("PLATFORM" "ROS_DISTRO" "MUTO_RS_IMAGE" "TS_AUTHKEY")
for var in "${required_muto_rs_vars[@]}"; do
    if grep -q "^$var=" config/.env.muto_rs.example; then
        check_pass ".env.muto_rs.example: $var defined"
    else
        check_fail ".env.muto_rs.example: $var missing"
    fi
done

required_ws_vars=("PLATFORM" "ROS_DISTRO" "MUTO_WS_IMAGE")
for var in "${required_ws_vars[@]}"; do
    if grep -q "^$var=" config/.env.workstation.example; then
        check_pass ".env.workstation.example: $var defined"
    else
        check_fail ".env.workstation.example: $var missing"
    fi
done

echo ""

# ─────────────────────────────────────────────────────────────────
# SECTION 9: Docker Compose Validation
# ─────────────────────────────────────────────────────────────────

echo "✔️  SECTION 9: Docker Compose Validation"
echo "─────────────────────────────────────────────────────────────"

if command -v docker-compose &> /dev/null || command -v docker &> /dev/null; then
    for file in docker/docker-compose.*.yml; do
        if docker compose config -f "$file" > /dev/null 2>&1; then
            check_pass "$file: Valid YAML syntax"
        else
            check_fail "$file: Invalid YAML syntax"
        fi
    done
else
    check_warn "docker-compose not available (skipping YAML validation)"
fi

echo ""

# ─────────────────────────────────────────────────────────────────
# SECTION 10: Test Simple Make Commands
# ─────────────────────────────────────────────────────────────────

echo "🧪 SECTION 10: Make Command Syntax"
echo "─────────────────────────────────────────────────────────────"

if make help > /dev/null 2>&1; then
    check_pass "make help: Executes without errors"
else
    check_fail "make help: Has syntax errors"
fi

if make check-docker > /dev/null 2>&1; then
    check_pass "make check-docker: Executes"
else
    check_warn "make check-docker: Failed (might need Docker installed)"
fi

echo ""

# ─────────────────────────────────────────────────────────────────
# SUMMARY
# ─────────────────────────────────────────────────────────────────

echo "═══════════════════════════════════════════════════════════════"
echo "📊 SUMMARY"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "✅ PASSED: $PASS"
echo "❌ FAILED: $FAIL"
echo "⚠️  WARNINGS: $WARN"
echo ""

if [ $FAIL -eq 0 ]; then
    if [ $WARN -eq 0 ]; then
        echo "🎉 ALL CHECKS PASSED! Repository is ready for deployment."
        exit 0
    else
        echo "⚠️  All critical checks passed, but $WARN warnings need attention."
        exit 0
    fi
else
    echo "❌ $FAIL checks failed. Please review and fix above."
    exit 1
fi
