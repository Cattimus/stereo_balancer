#include<windows.h>
#include<combaseapi.h>

#include<stdio.h>

#include "audio_capture.h"


int main()
{
    //initialize COM library in multithreaded mode
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    if (FAILED(hr))
    {
        printf("failed to initialize com library\n");
        return 1;
    }

    audio_capture test;
    test.poll_devices();
    test.begin();

    //cleanup com library
    CoUninitialize();
    return 0;
}