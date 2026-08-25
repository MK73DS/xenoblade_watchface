# Xenoblade Chronicles watchface for Pebble Time 2

A Pebble watchface for the Pebble Time 2 only, featuring a beautiful theme from the video game Xenoblade Chronicles!

![watchface animated gif during daytime](https://github.com/MK73DS/xenoblade_watchface/blob/main/screenshots/charging.gif?raw=true) ![watchface at night](https://github.com/MK73DS/xenoblade_watchface/blob/main/screenshots/night.png?raw=true) ![watchface adapting layout when Timeline preview is active](https://github.com/MK73DS/xenoblade_watchface/blob/main/screenshots/timeline.gif?raw=true)

## Features

- Time and date (obviously, it's a watch!). Updates every minute and seconds aren't displayed.
  - The time is displayed using the same font of the video game's damage numbers!

- Health metrics: total steps, total calories (idle + active), heart rate, sleep and deep sleep.
  - The health metrics are under the battle arts icons, recreated for this theme! They are all from original arts excepts the icon for steps, which is inspired by the "run away" icon of the game.
  - Deep sleep is shown under sleep

- Small but legible text thanks to a custom contour drawing algorithm! Small text feature a black border that makes them easier to read!

- Battery charge under the Monado art.

- Beautiful theme, inspired by the title screen of the game.
  - The clouds move to the left every minute (or second when charging), making the watch not exactly the same every time you look at it! (see images above)
  - The sky darkens during the night, just like in the video game! (see images above)

- The UI shifts accodringly when the Timeline preview appears on screen, nothing get hidden underneath! (see gif above)

- Performance and efficiency in mind: I want to use this watchface daily, so extra effort has been put in order for the watchface to be as efficient as possible, even for the text contouring and background scrolling features.

- The UI is automatically translated depending on your language setting! Supports English, French, Spanish, Portuguese, Italian and German!
  - The date will always be of the form DD/MM/YYYY.
  - _Note: I only speak French and English, if the translations for the other languages are not right, please tell me!_

_Note: I am not a developer, and I learned a lot of C during this project. I extensively searched and asked about my code but I cannot guarantee it is well written. If you want to suggest modifications, please do not hesitate to open a PR!_

## Limitations

- The watchface is not customizable (no settings page). You get what you see. It does adapt itself to the language of your watch however!

- Weather is not yet displayed, but that will be in the future when weather info can be requested from the PebbleOS instead of asking the connected smartphone. In particular, the day and night times are fixed for now (day starts at 7am and night at 9pm).

- I do not display bluetooth connection status because I don't need to, if more people want it I can add it.

## Disclaimers

- The background image was created by using a pixel art of @RoseyPixels as a source, although is was hevily modified for the purpose of this watchface. I was not able to contact the author on X in order to as permission as his account is private. If you are the author of this image and you disagree with the usage I made of it, please contact me.
  - All the other images were made by hand, inspired by the official assets of the game obtained via screen capturing footage of the game.

- All the code was written by me, with the help of documentation, forums, youtube videos and other resources. At no point an LLM was used to write code or to help me write code. This project is 100% human made.
