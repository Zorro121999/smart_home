smart_node (ESP32-h2)
ESP-IDF Version v5.3.2 with esp ZigBee SDK 2.0.4
1, Configured as ZigBee end-node
2, Reads Temperature, Humidity and Soil moisture data once every 10s and sends the data in a custom cluster to coordinator
3, Sleeps in between, in order to extend battery life

smart_gateway (ESP32-c6)
ESP-IDF Version v5.3.2 with esp ZigBee SDK 2.0.4
1, Configured as ZigBee coordinator
2, Configured as WIFI Station
3, Runs HTTP Server (Browser requests new data every 30s)