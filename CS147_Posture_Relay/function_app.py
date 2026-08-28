import azure.functions as func
import logging
import json
import os

from azure.iot.hub import IoTHubRegistryManager

app = func.FunctionApp()

DESK_DEVICE_ID = "desk-esp32"


@app.event_hub_message_trigger(
    arg_name="event",
    event_hub_name="Your Own",
    connection="IoTHubConnection",
    consumer_group="posture-relay-cg"
)
def chair_event_relay(event: func.EventHubEvent):

    try:
        logging.info("POSTURE RELAY FUNCTION RUNNING")

        # =================================================
        # READ CHAIR TELEMETRY
        # =================================================

        message = event.get_body().decode("utf-8")

        logging.info("Chair telemetry received:")
        logging.info(message)

        data = json.loads(message)

        # Only process chair ESP32 messages
        if data.get("device") != "chair-esp32":

            logging.info(
                "Ignoring message from another device."
            )

            return

        # =================================================
        # READ CURRENT STATES
        # =================================================

        posture_alert = data.get(
            "postureAlert",
            False
        )

        sitting_alert = data.get(
            "sittingAlert",
            False
        )

        overall_alert = data.get(
            "alert",
            False
        )

        alert_type = data.get(
            "alertType",
            "NONE"
        )

        # =================================================
        # BUILD MESSAGE FOR DESK ESP32
        # =================================================

        desk_message = json.dumps({

            "type": "CHAIR_STATUS",

            "posture": data.get(
                "posture",
                "UNKNOWN"
            ),

            "sittingSeconds": data.get(
                "sittingSeconds",
                0
            ),

            "badPostureSeconds": data.get(
                "badPostureSeconds",
                0
            ),

            "postureAlert": posture_alert,

            "sittingAlert": sitting_alert,

            "alert": overall_alert,

            "alertType": alert_type
        })

        # =================================================
        # CONNECT TO IOT HUB SERVICE
        # =================================================

        service_connection = os.environ[
            "IoTHubServiceConnection"
        ]

        registry_manager = (
            IoTHubRegistryManager.from_connection_string(
                service_connection
            )
        )

        # =================================================
        # SEND TO DESK ESP32
        # =================================================

        registry_manager.send_c2d_message(
            DESK_DEVICE_ID,
            desk_message
        )

        logging.info(
            "STATUS SENT TO DESK ESP32:"
        )

        logging.info(
            desk_message
        )

    except Exception as e:

        logging.exception(
            "Error while processing chair telemetry: %s",
            str(e)
        )