# main/CMakeLists.txt
set(srcs "sd_card_example_main.c"
		 "button.c")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       REQUIRES fatfs sd_card esp_driver_gpio sdmmc esp_timer
                       WHOLE_ARCHIVE)
