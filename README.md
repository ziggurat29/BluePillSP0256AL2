# BluePillSP0256AL2
A USB CDC interface to a physical SP0256-AL2 using Blue Pill.  Maybe later also a simulation and Text2Speech -- we'll see.

Project blog at [Hackaday.io Look Who's Talking 0256](https://hackaday.io/project/171417-look-whos-talking-0256)

This project was built with 'STM32CubeMX' and 'System Workbench for STM32'.  These are free tools from ST Microelectronics.  At the time of this writing, I am using 'STM32Cube MCU Package for STM32F1 Series' (a separate download from within CubeMX) version 1.8.0.  NOTE: ST can and does break their APIs version-to-version, so keep that in mind if regenerating from CubeMX suddenly causes subsequent compilation problems.  On that note, be also aware that I have some 'haques' to the stock STM32 firmware to make some peripherals behave the way I want.  These haques get overwritten each time you regenerate, alas, but to help manage that, there is a 'fixup.bat' in the '#fixups' directory that will re-apply those haques.  In particular, there is a dummy mechanism that is designed to break the build if you forget to do this (because otherwise it would build, but the system would simply not work).  If this happens to you, you'll know because I put a prominent comment at the line where the build breaks in this scenario and it explains what is going on and how to resolve it.

