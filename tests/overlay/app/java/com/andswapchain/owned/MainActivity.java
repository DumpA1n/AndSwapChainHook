package com.andswapchain.owned;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.TextView;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.List;

public final class MainActivity extends Activity {
    static { System.loadLibrary("andswapchain_overlay_fixture"); }
    private static int totalCycles;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private int baseline;
    private int polls;
    private TextView status;
    private static native int create(Activity activity);
    private static native int poll();
    private static native int exerciseAndDestroy();
    private static native void cleanup();
    private int windowRecords() throws Exception {
        Class<?> type=Class.forName("android.view.WindowManagerGlobal");
        Method get=type.getDeclaredMethod("getInstance");
        Field views=type.getDeclaredField("mViews");
        get.setAccessible(true); views.setAccessible(true);
        return ((List<?>)views.get(get.invoke(null))).size();
    }
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        status=new TextView(this);status.setText("Owned SurfaceView lifecycle fixture");setContentView(status);
        handler.postDelayed(this::begin,200);
    }
    private void require(boolean value,String message) { if(!value)throw new IllegalStateException(message); }
    private void begin() {
        try {
            baseline=windowRecords();
            require(create(this)==0,"Create failed");
            require(windowRecords()==baseline+1,"WindowManagerGlobal must own the added view");
            polls=0;handler.postDelayed(this::waitForSurface,16);
        } catch(Throwable error) { finishResult("FAIL "+error); }
    }
    private void waitForSurface() {
        try {
            int result=poll();
            if(result==3 && ++polls<200){handler.postDelayed(this::waitForSurface,16);return;}
            require(result==0,"Poll failed: "+result);
            int exercise=exerciseAndDestroy();
            require(exercise==0,"Lease/retirement contract failed: "+exercise);
            require(windowRecords()==baseline,"WindowManagerGlobal did not return to baseline");
            totalCycles++;
            if(totalCycles==5){recreate();return;}
            if(totalCycles<10){handler.postDelayed(this::begin,16);return;}
            finishResult("PASS cycles=10 activityReplacements=1 globals=0 windows=0 leases=0 WindowManagerGlobal=baseline");
        }catch(Throwable error){finishResult("FAIL "+error);}
    }
    private void finishResult(String message) {
        cleanup();status.setText(message);
        try(java.io.FileOutputStream file=openFileOutput("result.txt",MODE_PRIVATE)){file.write(message.getBytes(StandardCharsets.UTF_8));}
        catch(Exception error){throw new RuntimeException(error);}
    }
    @Override public void onDestroy(){handler.removeCallbacksAndMessages(null);cleanup();super.onDestroy();}
}
