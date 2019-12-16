// SOURCE:  https://github.com/keshavlohani/BluetoothSpp
// SEE:     https://www.youtube.com/watch?v=DhB9_MNgrpE

package com.example.ihabluetoothaudio;

import androidx.appcompat.app.AppCompatActivity;

import android.app.Activity;
import android.content.Intent;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Bundle;
import android.os.Handler;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.TextView;

import com.example.ihabluetoothaudio.bluetotohspp.library.BluetoothSPP;
import com.example.ihabluetoothaudio.bluetotohspp.library.BluetoothState;
import com.example.ihabluetoothaudio.bluetotohspp.library.DeviceList;

import java.nio.CharBuffer;
import java.nio.charset.Charset;
import java.util.Arrays;
import java.util.Timer;
import java.util.TimerTask;

public class MainActivity extends AppCompatActivity {
    private BluetoothSPP bt;

    private static final int block_size = 64;
    private static final int RECORDER_SAMPLERATE = 16000;
    private int AudioBufferSize = block_size * 4;
    private RingBuffer ringBuffer = new RingBuffer(AudioBufferSize * 4);
    private byte[] AudioBlock = new byte[AudioBufferSize];

    private int BufferElements2Rec = 2048; // block_size * 16; // want to play 2048 (2K) since 2 bytes we use only 1024
    private static final int RECORDER_CHANNELS = AudioFormat.CHANNEL_IN_STEREO;
    private static final int RECORDER_AUDIO_ENCODING = AudioFormat.ENCODING_PCM_16BIT;
    private AudioTrack audioTrack = null;

    private int AudioVolume = 0;
    private long lostBlockCount = 0, completeBlockCount = 0, countBlocks = 0, corruptBlocks = 0;
    private int countSamples = 0, BlockCount, transmittedCheckSum, ProtocolVersion, additionalBytesCount = 0;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        ((Button)findViewById(R.id.button_VolumeUp)).setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { AudioVolume += 1; setVolume(); }
        });
        ((Button)findViewById(R.id.button_VolumeDown)).setOnClickListener(new View.OnClickListener() {public void onClick(View v) { AudioVolume -= 1; setVolume(); }});
        ((CheckBox)findViewById(R.id.checkBox_adaptiveBitShift)).setOnClickListener(new View.OnClickListener() {public void onClick(View v) { setAdaptiveBitShift(); }});
        ((Button)findViewById(R.id.button_Link)).setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                Intent intent = new Intent(getApplicationContext(), DeviceList.class);
                startActivityForResult(intent, BluetoothState.REQUEST_CONNECT_DEVICE);
            }
        });
        initAudioTrack();
        initBluetooth();
    }

    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if(requestCode == BluetoothState.REQUEST_CONNECT_DEVICE && resultCode == Activity.RESULT_OK)
        {
            bt.connect(data);
            new Handler().postDelayed(new Runnable() {
                @Override
                public void run() {
                    bt.send(Charset.forName("UTF-8").encode(CharBuffer.wrap("STOREMAC")).array(), false);
                    new Handler().postDelayed(new Runnable() {
                        @Override
                        public void run() {
                            bt.disconnect();
                        }
                    }, 1000);
                }
            }, 2000);
        }
    }

    private void initBluetooth()
    {
        bt = new BluetoothSPP(this);
        if (bt.isBluetoothEnabled() == true) {
            bt.setBluetoothStateListener(new BluetoothSPP.BluetoothStateListener() {
                public void onServiceStateChanged(int state) {
                    if (state == BluetoothState.STATE_CONNECTED) {
                        BlockCount = 0;
                        ((TextView) findViewById(R.id.LogView)).append("Bluetooth State changed: STATE_CONNECTED\n");
                        Log.d("_IHA_", "Bluetooth Connected");
                        ((Button)findViewById(R.id.button_Link)).setEnabled(false);
                    } else if (state == BluetoothState.STATE_CONNECTING)
                        ((TextView) findViewById(R.id.LogView)).append("Bluetooth State changed: STATE_CONNECTING\n");
                    else if (state == BluetoothState.STATE_LISTEN)
                    {
                        ((TextView) findViewById(R.id.LogView)).append("Bluetooth State changed: STATE_LISTEN\n");
                        ((Button)findViewById(R.id.button_Link)).setEnabled(true);
                    }
                    else if (state == BluetoothState.STATE_NONE)
                        ((TextView) findViewById(R.id.LogView)).append("Bluetooth State changed: STATE_NONE\n");
                }
            });
            bt.setOnDataReceivedListener(new BluetoothSPP.OnDataReceivedListener() {
                public void onDataReceived(byte[] data) {
                    int DataBlockSize, lastBlockCount;
                    for (int i = 0; i < data.length; i++)
                    {
                        ringBuffer.addByte(data[i]);
                        countSamples++;
                        if (ringBuffer.getByte(0) == (byte) 0x00 && ringBuffer.getByte(-1) == (byte) 0x80) {
                            ProtocolVersion = ((ringBuffer.getByte(-4) & 0xFF) << 8) | (ringBuffer.getByte(-5) & 0xFF);
                            if (ProtocolVersion == 1)
                                additionalBytesCount = 12;
                        }
                        DataBlockSize = AudioBufferSize + additionalBytesCount;
                        if (ringBuffer.getByte(2 - DataBlockSize) == (byte) 0xFF && ringBuffer.getByte(1 - DataBlockSize) == (byte) 0x7F && ringBuffer.getByte(0) == (byte) 0x00 && ringBuffer.getByte(-1) == (byte) 0x80) {
                            countSamples = 0;
                            lastBlockCount = BlockCount;
                            AudioBlock = Arrays.copyOf(ringBuffer.data(3 - DataBlockSize, AudioBufferSize), AudioBufferSize);
                            AudioVolume = ((ringBuffer.getByte(-8) & 0xFF) << 8) | (ringBuffer.getByte(-9) & 0xFF);
                            BlockCount = ((ringBuffer.getByte(-6) & 0xFF) << 8) | (ringBuffer.getByte(-7) & 0xFF);
                            transmittedCheckSum = ((ringBuffer.getByte(-2) & 0xFF) << 8) | (ringBuffer.getByte(-3) & 0xFF);
                            if (lastBlockCount < BlockCount) {
                                lostBlockCount += BlockCount - (lastBlockCount + 1);
                                completeBlockCount += BlockCount - lastBlockCount;
                            }
                        } else if (countSamples == AudioBufferSize + additionalBytesCount) {
                            countSamples = 0;
                            corruptBlocks++;
                        }
                        if (countSamples == 0)
                        {
                            if (audioTrack != null)
                                audioTrack.write(AudioBlock, 0, AudioBufferSize);
                            countBlocks++;
                            runOnUiThread(new Runnable() {
                                @Override
                                public void run() {
                                    ((TextView) findViewById(R.id.textViewVolume)).setText(String.format("%d", AudioVolume));
                                    ((TextView) findViewById(R.id.textCorruptValue)).setText(String.format("%.3f", ((double) corruptBlocks / (double) countBlocks) * 100.0));
                                    ((TextView) findViewById(R.id.textLostValue)).setText(String.format("%.3f", ((double) lostBlockCount / (double) completeBlockCount) * 100.0));
                                    ((TextView) findViewById(R.id.textCorruptRealNumbersLabel)).setText(String.format("%d", corruptBlocks));
                                    ((TextView) findViewById(R.id.textLostRealNumbersLabel)).setText(String.format("%d", lostBlockCount));
                                }
                            });
                        }
                    }
                }
            });
            bt.setupService();
            bt.startService(BluetoothState.DEVICE_OTHER);
        }
        else{
            bt.enable();
            initBluetooth();
        }
    }

    private void setVolume()
    {
        AudioVolume = Math.max(Math.min(AudioVolume, 9), -9);
        byte[] bytes = Charset.forName("UTF-8").encode(CharBuffer.wrap("V+" + AudioVolume)).array();
        if (AudioVolume < 0)
            bytes = Charset.forName("UTF-8").encode(CharBuffer.wrap("V" + AudioVolume)).array();
        bt.send(bytes, false);
    }

    private void setAdaptiveBitShift()
    {
        CheckBox checkbox = (CheckBox)findViewById(R.id.checkBox_adaptiveBitShift);
        Button button1 = (Button)findViewById(R.id.button_VolumeUp);
        Button button2 = (Button)findViewById(R.id.button_VolumeDown);
        button1.setEnabled(!checkbox.isChecked());
        button2.setEnabled(!checkbox.isChecked());
        byte[] bytes = Charset.forName("UTF-8").encode(CharBuffer.wrap("B0")).array();
        if (checkbox.isChecked())
            bytes = Charset.forName("UTF-8").encode(CharBuffer.wrap("B1")).array();
        bt.send(bytes, false);
    }

    protected void initAudioTrack() {
        if (audioTrack == null) {
            audioTrack = new AudioTrack(
                    AudioManager.STREAM_MUSIC,
                    RECORDER_SAMPLERATE,
                    RECORDER_CHANNELS,
                    RECORDER_AUDIO_ENCODING,
                    BufferElements2Rec,
                    AudioTrack.MODE_STREAM);
            audioTrack.play();
        }
    }
}
