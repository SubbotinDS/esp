# main/CMakeLists.txt
set(srcs "main.c"
         "button.c"
         "recorder.c"
         "ring_buffer.c"
         "mic.c"
         "wav.c"
         "config.c")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       REQUIRES fatfs sd_card esp_driver_gpio sdmmc esp_timer esp_driver_i2s
                       WHOLE_ARCHIVE)
