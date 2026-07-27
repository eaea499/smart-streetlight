package com.smartstreetlight;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.context.properties.ConfigurationPropertiesScan;

@SpringBootApplication
@ConfigurationPropertiesScan
public class SmartStreetlightBackendApplication {

    public static void main(String[] args) {
        SpringApplication.run(SmartStreetlightBackendApplication.class, args);
    }
}
