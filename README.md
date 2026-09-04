# octoprint powersupply manager

## Physical circuit
I had a Ikea TRETAKT smart plug but not the remote or a smart home network, so i wired it to work from an ESP32-c3 instead.

First, i partially took apart the body of the socket to get the plastic button out of the way and leave the microswitch exposed.
On the button one of the sides is connected to a ground and the other measured 3V. I simply simulated a button press using a NPN transistor. It was a random one i had lying around so it may not be exactly optimal but it does not need to stay on for long anyway.

I soldered quite long wires to the button to connect it to a perfboard, which i placed on the screen of my printer.
I used a very primitive method to detect whether the printer is on - using a photoresistor pointed at the LCD since my prusa always leaves the screen on.

Then i added a few resistors to interface with the transistor and make the photoresistor easier to read.

![alt text](https://github.com/SumDoot/octoprint-powersupply-manager/blob/main/OctoPSUControllerLayout.png "Layout image")

## Code and setup
I made the code a bit primitive and didnt make the photoresistor reading thresholds adjustable on the HTML page so you may have to tinker a bit with it, sorry
It also has an MDNS set up, but that only worked during the setup part, not during actual usage :(

You can also use the button as a mechanical toggle or put the device into setup mode by holding it, it shouldnt mess up the octoprint setup unless youre actively printing i guess

Anyway, once the code is uploaded you can connect to your home network then find the ESPs IP address.

Then on the octoprint interface install the PSU control plugin

In the plugin settings for it the relevant fields will be for switching and sensing.
I am using octo4a on an older phone, if youre using something else the commands may be different and have to be changed.

For both the Switching Method and Sensing Method select System command.
Then enter these commands:

On System Command: ``` curl http:/192.168.1.162/on ```<br>
Off System Command: ``` curl http:/192.168.1.162/off ```<br>
Sensing System Command: ``` curl http://192.168.1.162/status | grep -q true ```<br>

Remember to change the ip to the one for your esp, mine probably wont work :)

You can also play around with the rest of the settings while youre here.
And, that should be it, you should be able to turn your printer on and off using the power button (lightning symbol)
