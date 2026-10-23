package com.securebrowser.platform.demo;

import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Profile;

@Configuration(proxyBeanMethods = false)
@Profile("demo")
@EnableConfigurationProperties(DemoProperties.class)
public class DemoConfiguration {
}
