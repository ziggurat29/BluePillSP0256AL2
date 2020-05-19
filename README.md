# BluePillSP0256AL2
A USB CDC interface to a physical SP0256-AL2 using Blue Pill.  Maybe later also a simulation and Text2Speech -- we'll see.

Project blog at [Hackaday.io Look Who's Talking 0256](https://hackaday.io/project/171417-look-whos-talking-0256)

This project was built with 'STM32CubeMX' and 'System Workbench for STM32'.  These are free tools from ST Microelectronics.  At the time of this writing, I am using 'STM32Cube MCU Package for STM32F1 Series' (a separate download from within CubeMX) version 1.8.0.  NOTE: ST can and does break their APIs version-to-version, so keep that in mind if regenerating from CubeMX suddenly causes subsequent compilation problems.  On that note, be also aware that I have some 'haques' to the stock STM32 firmware to make some peripherals behave the way I want.  These haques get overwritten each time you regenerate, alas, but to help manage that, there is a 'fixup.bat' in the '#fixups' directory that will re-apply those haques.  In particular, there is a dummy mechanism that is designed to break the build if you forget to do this (because otherwise it would build, but the system would simply not work).  If this happens to you, you'll know because I put a prominent comment at the line where the build breaks in this scenario and it explains what is going on and how to resolve it.

Using:

The project implements a 'command processor' which is on either USB CDC or UART1, as per build options.  In the DEBUG build, it is on UART1, and in RELEASE build it is on USB CDC.  (Debugging is easier over UART because halting execution also halts the USB driver, ultimately requiring re-enumeration.)

The 'command processor' provides a text-based command interface.  There are several commands:
*  help
  provide basic help info.  Used alone, this will list the commands available.  Used with a specific command, this will provide (very short) command-specific help.
*  set
  provides settings.  Used alone, this will list the current settings and their present values.  Used with a specific setting, this will allow changing the setting's value.
*  persist
  write the current settings to flash so they will stick across a reboot.
*  depersist
  read the settings from flash so they will revert to whatever they were before you fiddled with them.
*  reboot
  reboot the device
*  dump
  a diagnostic feature allowing reading from arbitrary memory locations
*  diag
  only available in DEBUG builds, this lists some internal values.  In particular, it shows RAM usage by various components and subsystems, and is used for tuning buffer sizes, etc, before final release.
*  ph
  accept a (hex) string of phonemes and emit them to the synthesizer (physical or simulated)
*  sp
  accept a quote-delimited text string and perform text-to-speech on it, sending the resulting phonemes to the synthesizesr (physical or simulated)
*  tts
  works like 'sp' except it prints the phoneme sequence generated.  This is more for debugging than actual use.
*  resetsp
  Reset the synthesizer (physical or simulated).  This will immediately halt phoneme output and purge any pending phonemes that may be buffered.  This is mostly useful for the physical synthesizer, because it can be left making sound (especially vowel sounds) and you might find this annoying and want it to immediately stop that.

Of the settings:
*  datetime
  set the current date and time of the RTC.  This is not particularly interesting in this project -- it simply came along for the ride from one of my other projects when I was re-using the command processor code.
*  spmode
  The speech processor may be set to 'physical', where an actual SP0256-AL2 is expected to be connected as per schematic.  This is the default mode at boot.
  It may also be set to 'simulated', where the PWM output on PB0 provides a signal similar to that of the output of the SP0256-AL2.  In this way, you can play with the synthesizer without having an actual chip (which have been long since out-of-production).  The output characteristics are similar to that of the original chip, so the same output filter and amp may be used.

