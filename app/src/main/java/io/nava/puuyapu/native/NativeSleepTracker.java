// NativeSleepTracker.java - High-performance JNI bridge
public class NativeSleepTracker {
    static {
        System.loadLibrary("puuyapu");
    }

    // Singleton for performance
    private static NativeSleepTracker instance;
    private static final Object lock = new Object();

    public static NativeSleepTracker getInstance() {
        if (instance == null) {
            synchronized (lock) {
                if (instance == null) {
                    instance = new NativeSleepTracker();
                    instance.initializeNative();
                }
            }
        }
        return instance;
    }

    // Native method declarations
    private native void initializeNative();

    public native long addInteractionEvent(long timestamp, int appType, long duration);

    public native SleepDetectionResult detectSleep(long[] timestamps, int[] types);

    public native double calculateConfidence(long bedtime, long wakeTime);

    public native void updateUserPreferences(
            double targetSleepHours,
            long preferredBedtime,
            long preferredWakeTime
    );

    // Batch processing for better performance
    public native void processBatchInteractions(
            long[] timestamps,
            int[] appTypes,
            long[] durations
    );

    // Real-time analysis
    public native boolean isCurrentlyAsleep();

    public native long getEstimatedSleepStart();

    // Memory management
    public native void clearOldData(long cutoffTimestamp);

    public native void optimizeMemory();
}