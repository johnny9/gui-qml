package org.bitcoincore.qt;

import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.view.WindowManager;
import android.view.View;

import org.qtproject.qt5.android.bindings.QtActivity;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;

public class BitcoinQtActivity extends QtActivity
{
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        final File bitcoinDir = new File(getFilesDir().getAbsolutePath() + "/.bitcoin");
        if (!bitcoinDir.exists()) {
            bitcoinDir.mkdir();
        }

        File bitcoinConf = new File(bitcoinDir, "bitcoin.conf");
        if (!bitcoinConf.exists()) {
            try {
                bitcoinConf.createNewFile();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }

        String configContent = "[signet]\n" +
                               "rpcauth=bitcoin:59d94c82dddb126b1aa5b1c9111f1eee$6e19cca78d04cd6175a5a5a59a2262e2aa8eeebee5682d79d7b548fde9af0063\n" +
                               "server=1\n" +
                               "rpcallowip=0.0.0.0/0\n" +
                               "rpcbind=0.0.0.0\n";

        try (FileWriter writer = new FileWriter(bitcoinConf)) {
            writer.write(configContent);
        } catch (IOException e) {
            e.printStackTrace();
        }

        Intent intent = new Intent(this, BitcoinQtService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_IMMERSIVE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN);
        super.onCreate(savedInstanceState);
    }
}
