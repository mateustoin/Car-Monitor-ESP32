idf_component_register(SRCS "replay_main.c"
                       REQUIRES obd_transport elm327 obd_poller gear_engine vehicle_profile)
