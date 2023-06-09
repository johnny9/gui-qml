package org.bitcoincore.qt;

import android.content.Context;
import android.app.NotificationManager;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.system.ErrnoException;
import android.system.Os;
import android.view.WindowManager;
import android.view.View;
import android.Manifest.permission;

import org.qtproject.qt5.android.bindings.QtActivity;

import java.io.File;

public class BitcoinQtActivity extends QtActivity
{
    private boolean notificationsEnabled = false;

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        final File bitcoinDir = new File(getFilesDir().getAbsolutePath() + "/.bitcoin");
        if (!bitcoinDir.exists()) {
            bitcoinDir.mkdir();
        }

        // Check if notifications are enabled
        /*
        NotificationManager notificationManager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (notificationManager != null && notificationManager.areNotificationsEnabled()) {
            enableNotifications();
        } else {
        }*/

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_IMMERSIVE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN);
    }

    @Override
    public void onStart(){
        super.onStart();
        register();
    }

    public void notificationsEnabledChanged(boolean enabled)
    {
        if (notificationsEnabled == enabled) {
            return;
        } else {
            notificationsEnabled = enabled;
        }

        if (notificationsEnabled) {
            Intent intent = new Intent(this, BitcoinQtService.class);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(intent);
            } else {
                startService(intent);
            }
        }
    }

    public native boolean showNotificationOnboarding();
    public native boolean register();
}
