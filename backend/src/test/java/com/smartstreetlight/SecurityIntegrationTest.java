package com.smartstreetlight;

import static org.hamcrest.Matchers.equalTo;
import static org.springframework.security.test.web.servlet.request.SecurityMockMvcRequestPostProcessors.csrf;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.autoconfigure.web.servlet.AutoConfigureMockMvc;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.mock.web.MockHttpSession;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.MvcResult;

@SpringBootTest(properties = {
        "mqtt.host=",
        "app.auth.demo-username=demo",
        "app.auth.demo-password=123456"
})
@AutoConfigureMockMvc
class SecurityIntegrationTest {

    @Autowired
    private MockMvc mockMvc;

    @Test
    void guestCanReadDevices() throws Exception {
        mockMvc.perform(get("/api/devices"))
                .andExpect(status().isOk());
    }

    @Test
    void guestCannotPublishDeviceCommand() throws Exception {
        mockMvc.perform(post("/api/devices/GLG-A-001/command")
                .with(csrf())
                        .contentType("application/json")
                        .content("{\"mode\":\"auto\"}"))
                .andExpect(status().isUnauthorized());
    }

    @Test
    void invalidDemoLoginIsRejected() throws Exception {
        mockMvc.perform(post("/api/auth/login")
                        .with(csrf())
                        .contentType("application/json")
                        .content("{\"username\":\"demo\",\"password\":\"wrong\"}"))
                .andExpect(status().isUnauthorized());
    }

    @Test
    void validDemoLoginCreatesAdminSession() throws Exception {
        mockMvc.perform(post("/api/auth/login")
                        .with(csrf())
                        .contentType("application/json")
                        .content("{\"username\":\"demo\",\"password\":\"123456\"}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.authenticated", equalTo(true)))
                .andExpect(jsonPath("$.role", equalTo("ADMIN")));
    }

    @Test
    void authenticatedAdminReachesProtectedCommandEndpoint() throws Exception {
        MvcResult login = mockMvc.perform(post("/api/auth/login")
                        .with(csrf())
                        .contentType("application/json")
                        .content("{\"username\":\"demo\",\"password\":\"123456\"}"))
                .andExpect(status().isOk())
                .andReturn();

        MockHttpSession session = (MockHttpSession) login.getRequest().getSession(false);
        mockMvc.perform(post("/api/devices/GLG-A-001/command")
                        .session(session)
                        .with(csrf())
                        .contentType("application/json")
                        .content("{\"mode\":\"auto\"}"))
                .andExpect(status().isServiceUnavailable());
    }
}
