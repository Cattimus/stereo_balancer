#pragma once

//wind32api includes
#include<windows.h>
#include<Mmdeviceapi.h>
#include<combaseapi.h>
#include<audioclient.h>
#include<mmreg.h>
#include<ksmedia.h>

//c++ standard library includes
#include<vector>
#include<string>
#include<thread>
#include<mutex>
using namespace std;

//c standard library includes
#include<stdio.h>
#include<stdint.h>
#include<stdlib.h>

class audio_capture
{
private:
    WAVEFORMATEXTENSIBLE format;

    //audio data buffers
    vector<uint8_t> input_data;
    vector<uint8_t> output_data;

    //output and input device interface ids
    wstring output_id;
    wstring input_id;

    mutex buffer_lock;

    void mix_streams(uint8_t* left_channel, uint8_t* right_channel);

public:

    HRESULT copy_data(uint8_t* input_data, int num_samples, int* done);
    HRESULT set_format(WAVEFORMATEX* to_set);

    void poll_devices();
    void begin();
    void play_stream();
};