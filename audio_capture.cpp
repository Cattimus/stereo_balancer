#include "audio_capture.h"

#include<initguid.h>
#include<Functiondiscoverykeys_devpkey.h>

// REFERENCE_TIME time units per second and per millisecond
static const uint64_t REFTIMES_PER_SEC = 10000000;
static const uint64_t REFTIMES_PER_MILLISEC = 10000;

//values needed by winapi
const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

//this function will copoy data from the shared loopback buffer and mix the stereo signals
HRESULT audio_capture::copy_data(uint8_t* data, int num_samples, int* done)
{
    if (data == NULL)
    {
        return S_OK;
    }

    //number of bytes to be read
    int num_bytes = format.Format.nBlockAlign * num_samples;

    int counter = 0;
    for (int i = 0; i < num_bytes; i++)
    {
        input_data.push_back(data[i]);
        counter++;

        //mix streams every nth byte in realtime (almost always 8)
        if (counter == format.Format.nBlockAlign)
        {
            mix_streams(input_data.data() + input_data.size() - format.Format.nBlockAlign, input_data.data() + input_data.size() - (format.Format.nBlockAlign / 2));
            counter = 0;
        }
    }

    return S_OK;
}

//helper function to properly copy the format data
HRESULT audio_capture::set_format(WAVEFORMATEX* to_set)
{
    if (to_set->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
    {
        format.Format = *to_set;
    }
    else
    {
        format = *reinterpret_cast<WAVEFORMATEXTENSIBLE*>(to_set);
    }

    return S_OK;
}

//mix the two channels together and form one new mono channel in both channels
void audio_capture::mix_streams(uint8_t* left_channel, uint8_t* right_channel)
{
    float left = 0;
    float right = 0;

    memcpy(&left, left_channel, sizeof(float));
    memcpy(&right, right_channel, sizeof(float));

    float mix = (float)((double)left + (double)right) / (double)2;

    memcpy(left_channel, &mix, sizeof(float));
    memcpy(right_channel, &mix, sizeof(float));

}

//get the list of all audio devices and have the user select input/output
void audio_capture::poll_devices()
{
    HRESULT result = S_OK;
    IMMDeviceEnumerator* enumerator = NULL;
    IMMDeviceCollection* collection = NULL;
    IMMDevice* endpoint = NULL;
    IPropertyStore* properties = NULL;
    LPWSTR device_ID = NULL;

    //for storing names
    vector<wstring> device_names;
    vector<wstring> device_ids;

    result = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&enumerator);
    if (!FAILED(result))
        result = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);

    UINT count = 0;
    if (!FAILED(result))
        result = collection->GetCount(&count);

    if (!FAILED(result))
    {
        for (UINT i = 0; i < count; i++)
        {
            result = collection->Item(i, &endpoint);
            if (FAILED(result)) { break; }

            result = endpoint->GetId(&device_ID);
            if (FAILED(result)) { break; }

            result = endpoint->OpenPropertyStore(STGM_READ, &properties);
            if (FAILED(result)) { break; }

            PROPVARIANT varName;
            PropVariantInit(&varName);

            //obtain human-readable device name
            result = properties->GetValue(PKEY_Device_FriendlyName, &varName);
            if (FAILED(result)) { break; }

            //add new device to list
            device_ids.push_back(wstring(device_ID));
            device_names.push_back(wstring(varName.pwszVal));

            //free memory from loop
            PropVariantClear(&varName);
            if (endpoint != NULL) { endpoint->Release(); endpoint = NULL; }
            if (properties != NULL) { properties->Release(); properties = NULL; }
            if (device_ID != NULL) { CoTaskMemFree(device_ID); device_ID = NULL; }
        }
    }

    if (FAILED(result))
        fprintf(stderr, "Error getting device names from win32 api\n");

    //free function memory
    if (device_ID != NULL) { CoTaskMemFree(device_ID); device_ID = NULL; }
    if (endpoint != NULL) { endpoint->Release(); endpoint = NULL; }
    if (properties != NULL) { properties->Release(); properties = NULL; }
    if (enumerator != NULL) { enumerator->Release(); enumerator = NULL; }
    if (collection != NULL) { collection->Release(); collection = NULL; }

    //select input and output device
    int selected_input = 0;
    int selected_output = 0;
    printf("System audio devices:\n");
    for (unsigned int i = 1; i < device_names.size() + 1; i++)
    {
        printf("%d: %S\n", i, device_names[i - 1].c_str());
    }

    printf("Please select audio input device: ");
    scanf_s("%d", &selected_input);
    selected_input--;

    printf("Please select audio output device: ");
    scanf_s("%d", &selected_output);
    selected_output--;

    printf("\n\n");
    printf("selected input device: %S\n", device_names[selected_input].c_str());
    printf("selected output device: %S\n", device_names[selected_output].c_str());

    input_id = device_ids[selected_input];
    output_id = device_ids[selected_output];
}

void audio_capture::begin()
{
    HRESULT hr;
    REFERENCE_TIME requested_duration = REFTIMES_PER_MILLISEC * 50;
    REFERENCE_TIME actual_duration = 0;
    UINT32 buffer_sample_count = 0;
    UINT32 samples_available = 0;
    IMMDeviceEnumerator* enumerator = NULL;
    IMMDevice* device = NULL;
    IAudioClient* audio_client = NULL;
    IAudioCaptureClient* capture_client = NULL;
    WAVEFORMATEX* wav_format = NULL;
    UINT32 packet_length = 0;
    BOOL done = FALSE;
    BYTE* data;
    DWORD flags;

    //initialize stream
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&enumerator);
    if (!FAILED(hr))
        hr = enumerator->GetDevice(input_id.c_str(), &device);
    if (!FAILED(hr))
        hr = device->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&audio_client);
    if (!FAILED(hr))
        hr = audio_client->GetMixFormat(&wav_format);

    //open stream in loopback mode
    if (!FAILED(hr))
        hr = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, requested_duration, 0, wav_format, NULL);
    if (!FAILED(hr))
        hr = audio_client->GetBufferSize(&buffer_sample_count);
    if (!FAILED(hr))
        hr = audio_client->GetService(IID_IAudioCaptureClient, (void**)&capture_client);
    if (!FAILED(hr))
        hr = set_format(wav_format);

    // Calculate the actual duration of the allocated buffer.
    if (!FAILED(hr))
        actual_duration = (double)REFTIMES_PER_SEC * buffer_sample_count / format.Format.nSamplesPerSec;
    if (!FAILED(hr))
        hr = audio_client->Start();

    if (!FAILED(hr))
    {
        thread output = thread(&audio_capture::play_stream, this);
        output.detach();
    }

    if (!FAILED(hr))
    {
        while (done == FALSE)
        {
            Sleep(actual_duration/REFTIMES_PER_MILLISEC/2);

            hr = capture_client->GetNextPacketSize(&packet_length);
            if (FAILED(hr)) { break; }

            while (packet_length != 0)
            {
                hr = capture_client->GetBuffer(&data, &samples_available, &flags, NULL, NULL);
                if (FAILED(hr)) { break; }

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                {
                    data = NULL;
                }

                hr = copy_data(data, samples_available, &done);
                if (FAILED(hr)) { break; }
                hr = capture_client->ReleaseBuffer(samples_available);
                if (FAILED(hr)) { break; }
                hr = capture_client->GetNextPacketSize(&packet_length);
                if (FAILED(hr)) { break; }
            }
            if (FAILED(hr)) { break; }

            //move current data to output buffer
            //synced using mutex
            if (this->input_data.size() > 1)
            {
                buffer_lock.lock();
                output_data.swap(input_data);
                buffer_lock.unlock();
            }
        }
    }

    if (!FAILED(hr))
        hr = audio_client->Stop();

    //free memory
    CoTaskMemFree(wav_format);
    if (device != NULL) { device->Release(); device = NULL; }
    if (enumerator != NULL) { enumerator->Release(); enumerator = NULL; }
    if (audio_client != NULL) { audio_client->Release(); audio_client = NULL; }
    if (capture_client != NULL) { capture_client->Release(); capture_client = NULL; }

    if (FAILED(hr))
    {
        printf("Error in audio capture encountered.\n");
    }
}

//TO-DO - account for different audio sample rates in the output device, potentially could result in failures
void audio_capture::play_stream()
{
    HRESULT hr;
    REFERENCE_TIME requested_duration = REFTIMES_PER_MILLISEC * 50;
    REFERENCE_TIME actual_duration;
    IMMDeviceEnumerator* enumerator = NULL;
    IMMDevice* device = NULL;
    IAudioClient* audio_client = NULL;
    IAudioRenderClient* render_client = NULL;
    WAVEFORMATEX* wave_format = NULL;
    UINT32 buffer_sample_count;
    UINT32 samples_available;
    UINT32 samples_padding;
    BYTE* data = NULL;
    DWORD flags = 0;

    uint32_t buffer_size = output_data.size();

    //initialize stream
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&enumerator);
    if (!FAILED(hr))
        hr = enumerator->GetDevice(output_id.c_str(), &device);
    if (!FAILED(hr))
        hr = device->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&audio_client);
    if (!FAILED(hr))
        hr = audio_client->GetMixFormat(&wave_format);

    //open stream in shared mode
    if (!FAILED(hr))
        hr = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, requested_duration, 0, wave_format, NULL);
    if (!FAILED(hr))
        hr = audio_client->GetBufferSize(&buffer_sample_count);
    if (!FAILED(hr))
        hr = audio_client->GetService(IID_IAudioRenderClient, (void**)&render_client);
    if (!FAILED(hr))
        hr = audio_client->Start();

    //lock mutex so that reading thread cannot copy data while it's being read
    buffer_lock.lock();

    if (!FAILED(hr))
    {
        while (flags != AUDCLNT_BUFFERFLAGS_SILENT)
        {
            hr = audio_client->GetCurrentPadding(&samples_padding);
            if (FAILED(hr)) { break; }

            samples_available = buffer_sample_count - samples_padding;
            hr = render_client->GetBuffer(samples_available, &data);
            if (FAILED(hr)) { break; }

            //copy buffer data
            if (data != NULL && buffer_size != 0)
            {
                int num_bytes = samples_available * format.Format.nBlockAlign;

                if (num_bytes > output_data.size())
                {
                    num_bytes = output_data.size();

                    //update the buffer size to prevent glitchy audio
                    samples_available = num_bytes / format.Format.nBlockAlign;
                }

                memcpy(data, output_data.data(), num_bytes);
                output_data.erase(output_data.begin(), output_data.begin() + num_bytes);
                buffer_size = output_data.size();
            }

            hr = render_client->ReleaseBuffer(samples_available, flags);
            if (FAILED(hr)) { break; }

            if (buffer_size == 0)
            {
                buffer_lock.unlock();

                //truncate the data buffer to silence to prevent glitchy and repeating audio
                memset(data, 0, (uint64_t)samples_available * format.Format.nBlockAlign);

                //wait until the reading thread has more audio for playback
                while (buffer_size == 0)
                {
                    buffer_lock.lock();
                    buffer_size = output_data.size();
                    buffer_lock.unlock();

                    Sleep(1);
                }

                //lock the mutex to prevent the reading thread from writing data to buffer until we're ready
                buffer_lock.lock();
            }

        }
    }
    if (!FAILED(hr))
        hr = audio_client->Stop();

    //clean up
    CoTaskMemFree(wave_format);
    if (device != NULL) { device->Release(); device = NULL; }
    if (enumerator != NULL) { enumerator->Release(); enumerator = NULL; }
    if (audio_client != NULL) { audio_client->Release(); audio_client = NULL; }
    if (render_client != NULL) { render_client->Release(); render_client = NULL; }

    if (FAILED(hr))
    {
        printf("Failure in audio playback.\n");
    }
}