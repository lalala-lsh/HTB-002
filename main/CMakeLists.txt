idf_component_register(SRCS 
                        "app_main.c"
                        "My_system.c"

                        "blufi/blufi.c"
                        "blufi/blufi_init.c"
                        "blufi/blufi_security.c"

                        "camera/camera.c"

                        "ClockData/clock.c"
                        "ClockData/ds1302.c"
                        "ClockData/imgfile_save.c"
                        "ClockData/protocal.c"
                        "ClockData/storage.c"

                        "factory/factory.c"
                        
                        "function/feed.c"
                        "function/heating.c"
                        "function/led_status.c"
                        
                        "key/key.c"

                        "power_manager/power_manager.c"

                        "server/image_transfer.c"
                        "server/mqtt.c"
                        "server/ota.c"

                        "train/model.cc"

                        "xl9535/xl9535.c"

                        "voice/voice.c"

                    INCLUDE_DIRS "."
                                "blufi/"
                                "camera/"
                                "ClockData/"
                                "function/"
                                "key/"
                                "power_manager/"
                                "server/"
                                "xl9535/"
                                "factory/"
                                "voice/"
                                "train/"

                    # Embed the server root certificate into the final binary
                    EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem)
