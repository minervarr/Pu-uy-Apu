#!/bin/sh

# Create the base Java package directory
BASE_DIR="app/src/main/java/io/nava/puuyapu"

# Create native interface files
mkdir -p "$BASE_DIR/native"
touch "$BASE_DIR/native/NativeSleepTracker.java"
touch "$BASE_DIR/native/NativeDataProcessor.java"
touch "$BASE_DIR/native/NativeCallbackHandler.java"

# Create services
mkdir -p "$BASE_DIR/services"
touch "$BASE_DIR/services/SleepTrackingService.java"
touch "$BASE_DIR/services/InteractionMonitorService.java"
touch "$BASE_DIR/services/DataSyncService.java"

# Create data layer structure
mkdir -p "$BASE_DIR/data/database/entities"
touch "$BASE_DIR/data/database/SleepDatabase.java"

mkdir -p "$BASE_DIR/data/repository"
touch "$BASE_DIR/data/repository/SleepRepository.java"
touch "$BASE_DIR/data/repository/NativeDataBridge.java"

mkdir -p "$BASE_DIR/data/cache"
touch "$BASE_DIR/data/cache/FastMemoryCache.java"

# Create UI components
mkdir -p "$BASE_DIR/ui/dashboard"
touch "$BASE_DIR/ui/dashboard/DashboardFragment.java"
touch "$BASE_DIR/ui/dashboard/DashboardViewModel.java"

mkdir -p "$BASE_DIR/ui/settings"
touch "$BASE_DIR/ui/settings/SettingsFragment.java"
touch "$BASE_DIR/ui/settings/SettingsViewModel.java"

mkdir -p "$BASE_DIR/ui/components"
touch "$BASE_DIR/ui/components/PerformanceChart.java"
touch "$BASE_DIR/ui/components/RealTimeDisplay.java"

# Create MainActivity
touch "$BASE_DIR/MainActivity.java"

# Print confirmation
echo "Java package structure created successfully:"
find app/src/main/java/io/nava/puuyapu -print | sed -e 's;[^/]*/;|____;g;s;____|; |;g'
