# SPDX-FileCopyrightText: 2025 Your Name <your.email@example.com>
#
# SPDX-License-Identifier: Unlicense

idf_component_register(
    SRCS "spi_ffs_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES spiffs nvs_flash spiffs
)
