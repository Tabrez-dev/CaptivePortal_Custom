This change introduces AWS IoT integration throughout the project. It adds a new `aws_iot` component with certificate management, MQTT client functionality, and public APIs for publishing and subscribing to AWS IoT topics. The main application and local server are updated to interact with AWS IoT, including new HTTP endpoints, message handling, and UI updates. Build configurations and test infrastructure are extended to support these features.

## Changes

| Cohort / File(s)                                                                                                                                                                                                                                         | Change Summary                                                                                                                                                                                |
| -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **AWS IoT Component Addition**<br>`components/aws_iot/aws_iot.c`<br>`components/aws_iot/include/aws_iot.h`<br>`components/aws_iot/CMakeLists.txt`<br>`components/aws_iot/certs/README.md`                                                                | Introduces a new AWS IoT component with MQTT client logic, public API, build configuration, and certificate usage documentation.                                                              |
| **AWS IoT Certificate Management**<br>`.gitignore`<br>`components/aws_iot/certs/README.md`                                                                                                                                                               | Adds rules to ignore certificate files except the README, and documents certificate usage and security practices.                                                                             |
| **Submodule Integration**<br>`.gitmodules`<br>`components/esp-aws-iot`<br>`esp-aws-iot`                                                                                                                                                                  | Adds Git submodules for the `esp-aws-iot` repository.                                                                                                                        |
| **App Local Server AWS IoT Integration**<br>`components/app_local_server/app_local_server.c`<br>`components/app_local_server/include/app_local_server.h`<br>`components/app_local_server/CMakeLists.txt`<br>`components/app_local_server/webpage/app.js` | Integrates AWS IoT: adds HTTP endpoint for status, updates sensor handler to publish to AWS IoT, modifies caching headers, extends enums, and updates the frontend to display AWS IoT status. |
| **App Time Sync AWS IoT Integration**<br>`components/app_time_sync/app_time_sync.c`<br>`components/app_time_sync/CMakeLists.txt`                                                                                                                         | Adds AWS IoT initialization right after time sync and declares the dependency in the build configuration.                                                                                     |
| **Main Application AWS IoT Integration**<br>`main/main.c`<br>`main/CMakeLists.txt`                                                                                                                                                                       | Adds AWS IoT message handler for commands, registers the callback, and updates build dependencies.                                                                                            |
| **AWS IoT Component Testing**<br>`components/aws_iot/test/aws_iot_test.c`<br>`components/aws_iot/test/CMakeLists.txt`                                                                                                                                    | Adds a Unity-based test verifying AWS IoT startup after Wi‑Fi connection, with necessary build setup.                                                                                         |

sequenceDiagram
    participant User
    participant Web Browser
    participant Local Server (HTTP)
    participant AWS IoT Component
    participant AWS IoT Cloud

    User->>Web Browser: Open web UI
    Web Browser->>Local Server (HTTP): GET /awsIoTStatus
    Local Server (HTTP)->>AWS IoT Component: aws_iot_is_connected()
    AWS IoT Component-->>Local Server (HTTP): Status, Client ID
    Local Server (HTTP)-->>Web Browser: JSON status
    Web Browser->>Web Browser: Update UI with status

    Note over Web Browser,Local Server (HTTP): Every 30 seconds, status is refreshed

    Web Browser->>Local Server (HTTP): GET /sensor
    Local Server (HTTP)->>AWS IoT Component: aws_iot_publish_sensor_data()
    AWS IoT Component->>AWS IoT Cloud: Publish sensor data
    AWS IoT Cloud-->>AWS IoT Component: (optional) Command message
    AWS IoT Component->>Main Application: aws_iot_message_handler()
    Main Application->>Main Application: Parse and act on command
