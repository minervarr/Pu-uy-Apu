package io.nava.puuyapu.app;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.Fragment;
import androidx.fragment.app.FragmentTransaction;

import android.Manifest;
import android.app.AppOpsManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.util.Log;
import android.view.View;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import android.os.Bundle;

import com.google.android.material.bottomnavigation.BottomNavigationView;
import com.google.android.material.snackbar.Snackbar;

import io.nava.puuyapu.app.services.SleepTrackingService;
import io.nava.puuyapu.app.ui.dashboard.DashboardFragment;
import io.nava.puuyapu.app.ui.settings.SettingsFragment;
import io.nava.puuyapu.app.ui.sleeplog.SleepLogFragment;

/**
 * Main Activity - Entry point for Puñuy Apu Sleep Tracker
 *
 * Responsibilities:
 * - Initialize native C++ sleep detection engine
 * - Handle critical permissions (Usage Stats, Background processing)
 * - Manage fragment navigation and bottom navigation
 * - Start and monitor background sleep tracking service
 * - Provide first-time user onboarding experience
 * - Handle app lifecycle for optimal performance
 *
 * Architecture:
 * - MVVM pattern with ViewModels for data management
 * - Fragment-based navigation with single Activity
 * - Native engine integration through JNI bridge
 * - Background service management for continuous tracking
 *
 * Performance Considerations:
 * - Native engine initialization on background thread
 * - Lazy fragment loading for faster startup
 * - Memory-efficient fragment management
 * - Optimized permission flow to minimize user friction
 *
 * @author Puñuy Apu Development Team
 * @version 1.0
 * @see NativeSleepTracker
 * @see SleepTrackingService
 */
public class MainActivity extends AppCompatActivity {
    private static final String TAG = "MainActivity";

    // Permission request codes
    private static final int REQUEST_USAGE_STATS_PERMISSION = 1001;
    private static final int REQUEST_STORAGE_PERMISSION = 1002;
    private static final int REQUEST_BACKGROUND_PERMISSION = 1003;

    // Fragment tags for navigation
    private static final String FRAGMENT_DASHBOARD = "dashboard";
    private static final String FRAGMENT_SLEEP_LOG = "sleep_log";
    private static final String FRAGMENT_SETTINGS = "settings";

    // Instance state keys
    private static final String STATE_CURRENT_FRAGMENT = "current_fragment";
    private static final String STATE_NATIVE_INITIALIZED = "native_initialized";
    private static final String STATE_ONBOARDING_COMPLETED = "onboarding_completed";

    // UI Components
    private BottomNavigationView bottomNavigation;
    private View loadingView;
    private View permissionPromptView;

    // App state
    private String currentFragmentTag = FRAGMENT_DASHBOARD;
    private boolean nativeEngineInitialized = false;
    private boolean onboardingCompleted = false;
    private boolean serviceStarted = false;

    // Native engine reference
    private NativeSleepTracker nativeSleepTracker;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Log.i(TAG, "MainActivity onCreate - Puñuy Apu Sleep Tracker starting");

        // Set content view
        setContentView(R.layout.activity_main);

        // Restore instance state
        if (savedInstanceState != null) {
            currentFragmentTag = savedInstanceState.getString(STATE_CURRENT_FRAGMENT, FRAGMENT_DASHBOARD);
            nativeEngineInitialized = savedInstanceState.getBoolean(STATE_NATIVE_INITIALIZED, false);
            onboardingCompleted = savedInstanceState.getBoolean(STATE_ONBOARDING_COMPLETED, false);
        }

        // Initialize UI components
        initializeViews();

        // Setup navigation
        setupBottomNavigation();

        // Initialize app based on state
        if (onboardingCompleted) {
            initializeMainApp();
        } else {
            startOnboardingFlow();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();

        Log.d(TAG, "MainActivity onResume");

        // Check if permissions were granted while app was paused
        if (onboardingCompleted && !nativeEngineInitialized) {
            if (hasRequiredPermissions()) {
                initializeNativeEngine();
            }
        }

        // Ensure background service is running
        if (nativeEngineInitialized && !serviceStarted) {
            startBackgroundTracking();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        Log.d(TAG, "MainActivity onPause");
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        Log.i(TAG, "MainActivity onDestroy");

        // Cleanup native resources (service will continue running)
        if (nativeSleepTracker != null) {
            // Note: Don't cleanup completely as background service needs it
            // Just optimize memory usage
            nativeSleepTracker.optimizeMemory();
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);

        outState.putString(STATE_CURRENT_FRAGMENT, currentFragmentTag);
        outState.putBoolean(STATE_NATIVE_INITIALIZED, nativeEngineInitialized);
        outState.putBoolean(STATE_ONBOARDING_COMPLETED, onboardingCompleted);
    }

    /**
     * Initialize UI components and views
     */
    private void initializeViews() {
        // Find views
        bottomNavigation = findViewById(R.id.bottom_navigation);
        loadingView = findViewById(R.id.loading_view);
        permissionPromptView = findViewById(R.id.permission_prompt_view);

        // Setup loading view
        if (loadingView != null) {
            loadingView.setVisibility(View.VISIBLE);
        }

        // Initially hide bottom navigation until setup is complete
        if (bottomNavigation != null) {
            bottomNavigation.setVisibility(View.GONE);
        }
    }

    /**
     * Setup bottom navigation with fragment switching
     */
    private void setupBottomNavigation() {
        if (bottomNavigation == null) return;

        bottomNavigation.setOnItemSelectedListener(item -> {
            int itemId = item.getItemId();

            if (itemId == R.id.nav_dashboard) {
                switchToFragment(FRAGMENT_DASHBOARD);
                return true;
            } else if (itemId == R.id.nav_sleep_log) {
                switchToFragment(FRAGMENT_SLEEP_LOG);
                return true;
            } else if (itemId == R.id.nav_settings) {
                switchToFragment(FRAGMENT_SETTINGS);
                return true;
            }

            return false;
        });
    }

    /**
     * Start onboarding flow for new users
     */
    private void startOnboardingFlow() {
        Log.i(TAG, "Starting onboarding flow");

        // Show welcome message
        showLoadingView(false);
        showWelcomeDialog();
    }

    /**
     * Initialize main app after onboarding
     */
    private void initializeMainApp() {
        Log.i(TAG, "Initializing main app");

        // Check permissions first
        if (!hasRequiredPermissions()) {
            requestRequiredPermissions();
            return;
        }

        // Initialize native engine
        initializeNativeEngine();

        // Setup main UI
        setupMainInterface();

        // Start background tracking
        startBackgroundTracking();
    }

    /**
     * Check if all required permissions are granted
     */
    private boolean hasRequiredPermissions() {
        return hasUsageStatsPermission() && hasStoragePermissions();
    }

    /**
     * Check if Usage Stats permission is granted
     */
    private boolean hasUsageStatsPermission() {
        AppOpsManager appOps = (AppOpsManager) getSystemService(Context.APP_OPS_SERVICE);
        int mode = appOps.checkOpNoThrow(AppOpsManager.OPSTR_GET_USAGE_STATS,
                android.os.Process.myUid(), getPackageName());
        return mode == AppOpsManager.MODE_ALLOWED;
    }

    /**
     * Check if storage permissions are granted
     */
    private boolean hasStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            // Android 13+ uses scoped storage, no permission needed for app files
            return true;
        } else {
            return ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
                    == PackageManager.PERMISSION_GRANTED;
        }
    }

    /**
     * Request all required permissions
     */
    private void requestRequiredPermissions() {
        Log.i(TAG, "Requesting required permissions");

        if (!hasUsageStatsPermission()) {
            requestUsageStatsPermission();
        } else if (!hasStoragePermissions()) {
            requestStoragePermissions();
        }
    }

    /**
     * Request Usage Stats permission
     */
    private void requestUsageStatsPermission() {
        showPermissionExplanationDialog(
                getString(R.string.permission_usage_title),
                getString(R.string.permission_usage_explanation),
                () -> {
                    Intent intent = new Intent(Settings.ACTION_USAGE_ACCESS_SETTINGS);
                    startActivityForResult(intent, REQUEST_USAGE_STATS_PERMISSION);
                }
        );
    }

    /**
     * Request storage permissions for data export
     */
    private void requestStoragePermissions() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            ActivityCompat.requestPermissions(this,
                    new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE},
                    REQUEST_STORAGE_PERMISSION);
        }
    }

    /**
     * Initialize native C++ sleep detection engine
     */
    private void initializeNativeEngine() {
        if (nativeEngineInitialized) {
            Log.d(TAG, "Native engine already initialized");
            return;
        }

        Log.i(TAG, "Initializing native sleep detection engine");
        showLoadingView(true);

        // Initialize on background thread to avoid blocking UI
        new Thread(() -> {
            try {
                // Get native tracker instance (triggers initialization)
                nativeSleepTracker = NativeSleepTracker.getInstance();

                // Verify initialization was successful
                if (nativeSleepTracker.isInitialized()) {
                    nativeEngineInitialized = true;

                    runOnUiThread(() -> {
                        Log.i(TAG, "Native engine initialized successfully");
                        onNativeEngineReady();
                    });
                } else {
                    throw new RuntimeException("Native engine failed to initialize");
                }

            } catch (Exception e) {
                Log.e(TAG, "Failed to initialize native engine", e);

                runOnUiThread(() -> {
                    showNativeEngineError(e.getMessage());
                });
            }
        }, "NativeInit").start();
    }

    /**
     * Called when native engine is ready
     */
    private void onNativeEngineReady() {
        Log.i(TAG, "Native engine ready - setting up main interface");

        // Hide loading view
        showLoadingView(false);

        // Setup main interface
        setupMainInterface();

        // Start background tracking
        startBackgroundTracking();

        // Show success message
        Toast.makeText(this, R.string.native_engine_ready, Toast.LENGTH_SHORT).show();
    }

    /**
     * Setup main user interface after initialization
     */
    private void setupMainInterface() {
        Log.d(TAG, "Setting up main interface");

        // Show bottom navigation
        if (bottomNavigation != null) {
            bottomNavigation.setVisibility(View.VISIBLE);
        }

        // Load initial fragment
        switchToFragment(currentFragmentTag);

        // Update navigation selection
        updateBottomNavigationSelection();
    }

    /**
     * Start background sleep tracking service
     */
    private void startBackgroundTracking() {
        if (serviceStarted) {
            Log.d(TAG, "Background tracking already started");
            return;
        }

        if (!nativeEngineInitialized) {
            Log.w(TAG, "Cannot start tracking - native engine not ready");
            return;
        }

        try {
            Log.i(TAG, "Starting background sleep tracking service");

            Intent serviceIntent = new Intent(this, SleepTrackingService.class);

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(serviceIntent);
            } else {
                startService(serviceIntent);
            }

            serviceStarted = true;
            Log.i(TAG, "Background tracking service started successfully");

        } catch (Exception e) {
            Log.e(TAG, "Failed to start background tracking service", e);
            showServiceStartError();
        }
    }

    /**
     * Switch to specified fragment
     */
    private void switchToFragment(String fragmentTag) {
        if (fragmentTag.equals(currentFragmentTag)) {
            Log.v(TAG, "Fragment " + fragmentTag + " already displayed");
            return;
        }

        Log.d(TAG, "Switching to fragment: " + fragmentTag);

        Fragment fragment = createFragmentForTag(fragmentTag);
        if (fragment == null) {
            Log.e(TAG, "Failed to create fragment for tag: " + fragmentTag);
            return;
        }

        FragmentTransaction transaction = getSupportFragmentManager().beginTransaction();

        // Add fade animation for smooth transitions
        transaction.setCustomAnimations(
                android.R.anim.fade_in,
                android.R.anim.fade_out
        );

        transaction.replace(R.id.fragment_container, fragment, fragmentTag);
        transaction.commit();

        currentFragmentTag = fragmentTag;
    }

    /**
     * Create fragment instance for given tag
     */
    private Fragment createFragmentForTag(String tag) {
        switch (tag) {
            case FRAGMENT_DASHBOARD:
                return new DashboardFragment();
            case FRAGMENT_SLEEP_LOG:
                return new SleepLogFragment();
            case FRAGMENT_SETTINGS:
                return new SettingsFragment();
            default:
                Log.w(TAG, "Unknown fragment tag: " + tag);
                return new DashboardFragment(); // Default fallback
        }
    }

    /**
     * Update bottom navigation selection to match current fragment
     */
    private void updateBottomNavigationSelection() {
        if (bottomNavigation == null) return;

        int itemId;
        switch (currentFragmentTag) {
            case FRAGMENT_DASHBOARD:
                itemId = R.id.nav_dashboard;
                break;
            case FRAGMENT_SLEEP_LOG:
                itemId = R.id.nav_sleep_log;
                break;
            case FRAGMENT_SETTINGS:
                itemId = R.id.nav_settings;
                break;
            default:
                itemId = R.id.nav_dashboard;
        }

        bottomNavigation.setSelectedItemId(itemId);
    }

    /**
     * Show or hide loading view
     */
    private void showLoadingView(boolean show) {
        if (loadingView != null) {
            loadingView.setVisibility(show ? View.VISIBLE : View.GONE);
        }
    }

    /**
     * Show welcome dialog for new users
     */
    private void showWelcomeDialog() {
        new androidx.appcompat.app.AlertDialog.Builder(this)
                .setTitle(R.string.welcome_title)
                .setMessage(R.string.welcome_message)
                .setPositiveButton(R.string.btn_continue, (dialog, which) -> {
                    dialog.dismiss();
                    onboardingCompleted = true;
                    requestRequiredPermissions();
                })
                .setCancelable(false)
                .show();
    }

    /**
     * Show permission explanation dialog
     */
    private void showPermissionExplanationDialog(String title, String message, Runnable onAccept) {
        new androidx.appcompat.app.AlertDialog.Builder(this)
                .setTitle(title)
                .setMessage(message)
                .setPositiveButton(R.string.btn_grant_permission, (dialog, which) -> {
                    dialog.dismiss();
                    onAccept.run();
                })
                .setNegativeButton(R.string.btn_cancel, (dialog, which) -> {
                    dialog.dismiss();
                    showPermissionDeniedMessage();
                })
                .setCancelable(false)
                .show();
    }

    /**
     * Show error when native engine fails to initialize
     */
    private void showNativeEngineError(String errorMessage) {
        Log.e(TAG, "Native engine error: " + errorMessage);

        showLoadingView(false);

        new androidx.appcompat.app.AlertDialog.Builder(this)
                .setTitle(R.string.error_native_engine_failed)
                .setMessage(getString(R.string.error_native_engine_failed) + "\n\n" + errorMessage)
                .setPositiveButton(R.string.btn_retry, (dialog, which) -> {
                    dialog.dismiss();
                    initializeNativeEngine();
                })
                .setNegativeButton(R.string.btn_cancel, (dialog, which) -> {
                    dialog.dismiss();
                    finish();
                })
                .setCancelable(false)
                .show();
    }

    /**
     * Show error when background service fails to start
     */
    private void showServiceStartError() {
        Snackbar.make(findViewById(R.id.main_container),
                        R.string.error_operation_failed,
                        Snackbar.LENGTH_LONG)
                .setAction(R.string.btn_retry, v -> startBackgroundTracking())
                .show();
    }

    /**
     * Show message when critical permissions are denied
     */
    private void showPermissionDeniedMessage() {
        new androidx.appcompat.app.AlertDialog.Builder(this)
                .setTitle(R.string.error_permission_denied)
                .setMessage(R.string.error_permission_required)
                .setPositiveButton(R.string.btn_permission_settings, (dialog, which) -> {
                    dialog.dismiss();
                    openAppSettings();
                })
                .setNegativeButton(R.string.btn_cancel, (dialog, which) -> {
                    dialog.dismiss();
                    finish();
                })
                .show();
    }

    /**
     * Open app settings for manual permission grant
     */
    private void openAppSettings() {
        Intent intent = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
        Uri uri = Uri.fromParts("package", getPackageName(), null);
        intent.setData(uri);
        startActivity(intent);
    }

    /**
     * Handle permission request results
     */
    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);

        Log.d(TAG, "Permission result: " + requestCode);

        switch (requestCode) {
            case REQUEST_STORAGE_PERMISSION:
                if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                    Log.i(TAG, "Storage permission granted");

                    // Check if all permissions are now granted
                    if (hasRequiredPermissions()) {
                        initializeMainApp();
                    }
                } else {
                    Log.w(TAG, "Storage permission denied");
                    showPermissionDeniedMessage();
                }
                break;
        }
    }

    /**
     * Handle activity results (primarily from settings screens)
     */
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        Log.d(TAG, "Activity result: " + requestCode);

        switch (requestCode) {
            case REQUEST_USAGE_STATS_PERMISSION:
                if (hasUsageStatsPermission()) {
                    Log.i(TAG, "Usage stats permission granted");

                    // Request next permission or continue setup
                    if (!hasStoragePermissions()) {
                        requestStoragePermissions();
                    } else {
                        initializeMainApp();
                    }
                } else {
                    Log.w(TAG, "Usage stats permission still denied");
                    showPermissionDeniedMessage();
                }
                break;
        }
    }

    /**
     * Handle back button press
     */
    @Override
    public void onBackPressed() {
        // If on dashboard, exit app
        if (FRAGMENT_DASHBOARD.equals(currentFragmentTag)) {
            super.onBackPressed();
        } else {
            // Navigate back to dashboard
            switchToFragment(FRAGMENT_DASHBOARD);
            if (bottomNavigation != null) {
                bottomNavigation.setSelectedItemId(R.id.nav_dashboard);
            }
        }
    }

    /**
     * Provide access to native sleep tracker for fragments
     */
    public NativeSleepTracker getNativeSleepTracker() {
        return nativeSleepTracker;
    }

    /**
     * Check if native engine is ready
     */
    public boolean isNativeEngineReady() {
        return nativeEngineInitialized && nativeSleepTracker != null;
    }

    /**
     * Public method to manually record sleep (called from fragments)
     */
    public void recordSleepManually() {
        if (!isNativeEngineReady()) {
            Log.w(TAG, "Cannot record sleep - native engine not ready");
            Toast.makeText(this, R.string.error_native_engine_unavailable, Toast.LENGTH_SHORT).show();
            return;
        }

        try {
            // Add manual sleep confirmation event
            long timestamp = System.currentTimeMillis();
            nativeSleepTracker.addInteractionEvent(
                    timestamp,
                    NativeSleepTracker.InteractionType.SLEEP_CONFIRMATION,
                    0 // Duration 0 for instant confirmation
            );

            Log.i(TAG, "Manual sleep confirmation recorded");
            Toast.makeText(this, R.string.sleep_confirmed, Toast.LENGTH_SHORT).show();

            // Refresh current fragment if it's dashboard
            if (FRAGMENT_DASHBOARD.equals(currentFragmentTag)) {
                Fragment currentFragment = getSupportFragmentManager().findFragmentByTag(currentFragmentTag);
                if (currentFragment instanceof DashboardFragment) {
                    ((DashboardFragment) currentFragment).refreshData();
                }
            }

        } catch (Exception e) {
            Log.e(TAG, "Failed to record manual sleep", e);
            Toast.makeText(this, R.string.error_operation_failed, Toast.LENGTH_SHORT).show();
        }
    }

    /**
     * Get current sleep status for fragments
     */
    public boolean isCurrentlyAsleep() {
        if (!isNativeEngineReady()) {
            return false;
        }

        try {
            return nativeSleepTracker.isCurrentlyAsleep();
        } catch (Exception e) {
            Log.e(TAG, "Error checking sleep status", e);
            return false;
        }
    }

    /**
     * Export sleep data (called from settings)
     */
    public void exportSleepData() {
        if (!isNativeEngineReady()) {
            Toast.makeText(this, R.string.error_native_engine_unavailable, Toast.LENGTH_SHORT).show();
            return;
        }

        // Export data on background thread
        new Thread(() -> {
            try {
                String jsonData = nativeSleepTracker.exportAllSleepData();

                runOnUiThread(() -> {
                    // Save to file and show success message
                    saveSleepDataToFile(jsonData);
                });

            } catch (Exception e) {
                Log.e(TAG, "Failed to export sleep data", e);

                runOnUiThread(() -> {
                    Toast.makeText(MainActivity.this, R.string.error_export_failed, Toast.LENGTH_SHORT).show();
                });
            }
        }, "DataExport").start();
    }

    /**
     * Save exported data to file
     */
    private void saveSleepDataToFile(String jsonData) {
        try {
            // Create filename with timestamp
            String filename = "puuyapu_sleep_data_" +
                    System.currentTimeMillis() + ".json";

            // Use app's external files directory (no permission needed on modern Android)
            java.io.File exportDir = getExternalFilesDir("exports");
            if (exportDir != null && !exportDir.exists()) {
                exportDir.mkdirs();
            }

            java.io.File exportFile = new java.io.File(exportDir, filename);

            // Write data to file
            try (java.io.FileWriter writer = new java.io.FileWriter(exportFile)) {
                writer.write(jsonData);
            }

            Log.i(TAG, "Sleep data exported to: " + exportFile.getAbsolutePath());

            // Show success message with file location
            Toast.makeText(this,
                    getString(R.string.export_success_location, exportFile.getAbsolutePath()),
                    Toast.LENGTH_LONG).show();

        } catch (Exception e) {
            Log.e(TAG, "Failed to save export file", e);
            Toast.makeText(this, R.string.error_export_failed, Toast.LENGTH_SHORT).show();
        }
    }

    /**
     * Get performance metrics for debugging
     */
    public String getPerformanceMetrics() {
        if (!isNativeEngineReady()) {
            return "{}";
        }

        try {
            return nativeSleepTracker.getPerformanceMetrics();
        } catch (Exception e) {
            Log.e(TAG, "Error getting performance metrics", e);
            return "{}";
        }
    }

    /**
     * Lifecycle method - handle app coming to foreground
     */
    @Override
    protected void onStart() {
        super.onStart();
        Log.d(TAG, "MainActivity onStart");

        // Log app start for interaction tracking
        if (isNativeEngineReady()) {
            nativeSleepTracker.addInteractionEvent(
                    System.currentTimeMillis(),
                    NativeSleepTracker.InteractionType.MEANINGFUL_USE,
                    0 // Duration will be calculated when app is paused
            );
        }
    }

    /**
     * Lifecycle method - handle app going to background
     */
    @Override
    protected void onStop() {
        super.onStop();
        Log.d(TAG, "MainActivity onStop");

        // This is important for sleep detection - app going to background
        // The background service will continue monitoring
    }

    /**
     * Handle low memory situations
     */
    @Override
    public void onLowMemory() {
        super.onLowMemory();
        Log.w(TAG, "Low memory warning - optimizing native engine");

        if (isNativeEngineReady()) {
            // Optimize memory usage in native engine
            nativeSleepTracker.optimizeMemory();

            // Clear old data to free up space
            nativeSleepTracker.clearOldData(30); // Keep last 30 days
        }
    }

    /**
     * Handle trim memory requests from system
     */
    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);

        Log.d(TAG, "Trim memory request: " + level);

        if (isNativeEngineReady()) {
            switch (level) {
                case TRIM_MEMORY_RUNNING_LOW:
                case TRIM_MEMORY_RUNNING_CRITICAL:
                    // Aggressive memory cleanup
                    nativeSleepTracker.clearOldData(14); // Keep last 2 weeks
                    nativeSleepTracker.optimizeMemory();
                    break;

                case TRIM_MEMORY_UI_HIDDEN:
                case TRIM_MEMORY_BACKGROUND:
                    // Moderate cleanup when app is in background
                    nativeSleepTracker.optimizeMemory();
                    break;
            }
        }
    }
}