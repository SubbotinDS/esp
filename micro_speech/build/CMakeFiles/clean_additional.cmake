# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "bootloader\\bootloader.bin"
  "bootloader\\bootloader.elf"
  "bootloader\\bootloader.map"
  "config\\sdkconfig.cmake"
  "config\\sdkconfig.h"
  "esp-idf\\mbedtls\\x509_crt_bundle"
  "flash_app_args"
  "flash_bootloader_args"
  "flash_project_args"
  "flasher_args.json"
  "flasher_args.json.in"
  "ldgen_libraries"
  "ldgen_libraries.in"
  "micro_speech.bin"
  "micro_speech.map"
  "no_1000ms.wav.S"
  "no_30ms.wav.S"
  "noise_1000ms.wav.S"
  "project_elf_src_esp32s3.c"
  "silence_1000ms.wav.S"
  "x509_crt_bundle.S"
  "yes_1000ms.wav.S"
  "yes_30ms.wav.S"
  )
endif()
