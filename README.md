# iPerf benchmark example for the ESP8266 
## Porting Notes

This is the iperf example that comes with the ESP32 IDF (on version 3.3, at $IDF_PATH/examples/wifi/iperf)
adapted to compile with the ESP8266 RTOS SDK (coincidentally, also version 3.3) and run on ESP8266 hardware.

Only server mode (ie, `iperf -s` on the ESP8266) was tested, but client mode (`iperf -c`) should also work.
Re: TCP mode vs UDP, TCP works well but I've seen some strange things with UDP (I think they also happen in the original version from Espressif).

Tested on NodeMCU-like hardware labeled "MODEL ESP-12F" and "MODEL ESP-12E" both "VENDOR DOITING", but should
also work on other ESP8266 hardware with sufficiently standard WiFi.

Curiously, on my tests, the ESP-12E (which is supposed to have an inferior, non-"optimized" antenna) had *better*
performance than the ESP-12F:

        ESP-12E:
                [ ID] Interval       Transfer     Bandwidth
                [  3]  0.0-10.1 sec  2.25 MBytes  1.86 Mbits/sec
        ESP-12F:
                [ ID] Interval       Transfer     Bandwidth
                [  3]  0.0-10.3 sec  3.38 MBytes  2.75 Mbits/sec

Also, the ESP-12F takes longer to connect to Wifi (over 1 second to set the interface address as part of DHCP, versus under 1 second for the ESP-12E).
So far, I'm frankly unimpressed with the "optimized" ESP-12F...

On both cases, the modules were operating in "sta" mode connected to the same WAP (an Android phone in hotspot mode),
and the client was a laptop running Linux Ubuntu 18.04 and iperf "version 2.0.10 (2 June 2018) pthreads".
The commands on both ESP8266 modules were:

        sta $SSID $PASSWORD
        iperf -s

and on the Linux machine:

        iperf -c $ESP8266_IP

## New autorun functionality
In order to facilitate headless/automated testing (ie, with no USB connected to the ESP8266 console), I implemented an 'autorun' facility; 
please check the `help` output for the autorun_* commands for details, but to use it to implement fully automated testing in server mode, you could use something like this:

	autorun_set "sta yourSSID yourPWD; autorun_delay 2000; iperf -s; autorun_wait iperf_traffic; restart"

## Contact
Your feedback regarding this port is appreciated, please contact me via the Github repo I opened for this project: github.com/DurvalMenezes/esp8266-iperf

Cheers,
-- Durval Menezes 2020/12/27.

[From this point on, it's the original ESP32 IDF iperf README.md]

# Iperf Example

## Note about iperf version
The iperf example doesn't support all features in standard iperf. It's compitable with iperf version 2.x.

## Note about 80MHz flash frequency
The iperf can get better throughput if the SPI flash frequency is set to 80MHz, but the system may crash in 80MHz mode for ESP-WROVER-KIT. 
Removing R140~R145 from the board can fix this issue. Currently the default SPI frequency is set to 40MHz, if you want to change the SPI flash 
frequency to 80MHz, please make sure R140~R145 are removed from ESP-WROVER-KIT or use ESP32 DevKitC.

## Introduction
This example implements the protocol used by the common performance measurement tool [iPerf](https://iperf.fr/). 
Performance can be measured between two ESP32s running this example, or between a single ESP32 and a computer running the iPerf tool

Demo steps to test station TCP Tx performance: 

1. Build the iperf example with sdkconfig.defaults, which contains performance test specific configurations

2. Run the demo as station mode and join the target AP
   sta ssid password

3. Run iperf as server on AP side
   iperf -s -i 3

4. Run iperf as client on ESP32 side
   iperf -c 192.168.10.42 -i 3 -t 60

The console output, which is printed by station TCP RX throughput test, looks like:

>esp32> sta aptest
>
>I (5325) iperf: sta connecting to 'aptest'
>
>esp32> I (6017) event: ip: 192.168.10.248, mask: 255.255.255.0, gw: 192.168.10.1
>
>esp32> iperf -s -i 3 -t 1000
>
>I (14958) iperf: mode=tcp-server sip=192.168.10.248:5001, dip=0.0.0.0:5001, interval=3, time=1000
>
>Interval Bandwidth
>
>esp32> accept: 192.168.10.42,62958
>
>0-   3 sec       8.43 Mbits/sec
>
>3-   6 sec       36.16 Mbits/sec
>
>6-   9 sec       36.22 Mbits/sec
>
>9-  12 sec       36.44 Mbits/sec
>
>12-  15 sec       36.25 Mbits/sec
>
>15-  18 sec       24.36 Mbits/sec
>
>18-  21 sec       27.79 Mbits/sec


Steps to test station/soft-AP TCP/UDP RX/TX throughput are similar as test steps in station TCP TX.

See the README.md file in the upper level 'examples' directory for more information about examples.
