import obspython as obs
import time

try:
  import serial
  from serial.tools import list_ports
except ImportError:
  serial = None
  list_ports = None


TIMER_INTERVAL_MS = 500

serial_conn = None
status_text = "Not connected"
last_timer = 0
obs_settings_object = None
obs_settings = {
  "serial_port": "",
  "baudrate": 9600,
  "brightness": 255,
  "scene_1": "",
  "scene_2": "",
  "scene_3": "",
  "scene_4": "",
}


def script_description():
  return """
  OBS Tally Lights

  Set the serial port and the baudrate (default 9600) of the connected OBS Tally Lights sender module. After that, specify one scene per tally light from the respecitve drop down menu.
  """


def script_defaults(settings):
  obs.obs_data_set_default_int(settings, "baudrate", 9600)
  obs.obs_data_set_default_int(settings, "brightness", 255)


def script_properties():
  props = obs.obs_properties_create()

  obs.obs_properties_add_list(
    props,
    "serial_port",
    "Serial Port",
    obs.OBS_COMBO_TYPE_LIST,
    obs.OBS_COMBO_FORMAT_STRING
  )
  fill_serial_ports(props)

  obs.obs_properties_add_button(
    props,
    "refresh_ports",
    "Refresh Ports",
    on_refresh_ports_clicked
  )

  obs.obs_properties_add_int(
    props,
    "baudrate",
    "Baudrate",
    300,
    2000000,
    1
  )

  obs.obs_properties_add_button(
    props,
    "reconnect_serial",
    "Reconnect Serial",
    on_serial_reconnect_clicked
  )

  obs.obs_properties_add_int_slider(
    props,
    "brightness",
    "Tally Brightness",
    0,
    255,
    1
  )

  obs.obs_properties_add_list(
    props,
    "scene_1",
    "Scene for Tally 1",
    obs.OBS_COMBO_TYPE_LIST,
    obs.OBS_COMBO_FORMAT_STRING
  )
  obs.obs_properties_add_list(
    props,
    "scene_2",
    "Scene for Tally 2",
    obs.OBS_COMBO_TYPE_LIST,
    obs.OBS_COMBO_FORMAT_STRING
  )
  obs.obs_properties_add_list(
    props,
    "scene_3",
    "Scene for Tally 3",
    obs.OBS_COMBO_TYPE_LIST,
    obs.OBS_COMBO_FORMAT_STRING
  )
  obs.obs_properties_add_list(
    props,
    "scene_4",
    "Scene for Tally 4",
    obs.OBS_COMBO_TYPE_LIST,
    obs.OBS_COMBO_FORMAT_STRING
  )
  fill_scenes(props)

  obs.obs_properties_add_button(
    props,
    "refresh_scenes",
    "Refresh Scenes",
    on_refresh_scenes_clicked
  )

  obs.obs_properties_add_text(
    props,
    "status",
    "Status",
    obs.OBS_TEXT_INFO
  )

  obs.obs_properties_add_button(
    props,
    "refresh_status",
    "Refresh Status",
    on_refresh_status_clicked
  )

  return props


def fill_serial_ports(props):
  prop = obs.obs_properties_get(props, "serial_port")
  obs.obs_property_list_clear(prop)

  if list_ports is None:
    obs.obs_property_list_add_string(prop, "-- pyserial not installed --", "")
    return
  
  ports = list_ports.comports()

  obs.obs_property_list_add_string(prop, "-- no port --", "")
  for port in ports:
    label = f"{port.device} - {port.description}"
    obs.obs_property_list_add_string(prop, label, port.device)


def fill_scenes(props):
  for prop in [
    obs.obs_properties_get(props, "scene_1"),
    obs.obs_properties_get(props, "scene_2"),
    obs.obs_properties_get(props, "scene_3"),
    obs.obs_properties_get(props, "scene_4")
  ]:
    obs.obs_property_list_clear(prop)
    obs.obs_property_list_add_string(prop, "-- no scene --", "")
    scenes = obs.obs_frontend_get_scenes()
    for scene in scenes:
      name = obs.obs_source_get_name(scene)
      obs.obs_property_list_add_string(prop, name, name)
      obs.obs_source_release(scene)


def script_load(settings):
  global obs_settings_object
  obs_settings_object = settings
  obs.obs_frontend_add_event_callback(on_frontend_event)
  obs.timer_add(on_timer, TIMER_INTERVAL_MS)


def script_unload():
  obs.timer_remove(on_timer)
  obs.obs_frontend_remove_event_callback(on_frontend_event)
  close_serial()


def script_update(settings):
  obs_settings["serial_port"] = obs.obs_data_get_string(settings, "serial_port")
  obs_settings["baudrate"] = obs.obs_data_get_int(settings, "baudrate")
  obs_settings["brightness"] = obs.obs_data_get_int(settings, "brightness")
  obs_settings["scene_1"] = obs.obs_data_get_string(settings, "scene_1")
  obs_settings["scene_2"] = obs.obs_data_get_string(settings, "scene_2")
  obs_settings["scene_3"] = obs.obs_data_get_string(settings, "scene_3")
  obs_settings["scene_4"] = obs.obs_data_get_string(settings, "scene_4")


def on_refresh_ports_clicked(props, prop):
  fill_serial_ports(props)
  return True


def on_serial_reconnect_clicked(props, prop):
  reconnect_serial()
  send_current_state()
  return True


def on_refresh_scenes_clicked(props, prop):
  fill_scenes(props)
  return True


def on_refresh_status_clicked(props, prop):
  update_status()
  return True

def update_status():
  obs.obs_data_set_string(obs_settings_object, "status", status_text)

def on_frontend_event(event):
  if event in (
    obs.OBS_FRONTEND_EVENT_SCENE_CHANGED,
    obs.OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED,
    obs.OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED,
    obs.OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED,
  ):
    send_current_state()


def on_timer():
  global last_timer

  now = time.monotonic()

  # discard backlogged on_timer executions
  if now - last_timer < TIMER_INTERVAL_MS / 1000 * 0.9:
    return

  last_timer = now
  send_current_state()


def get_scene_name(source):
  if source is None:
    return ""

  name = obs.obs_source_get_name(source)
  obs.obs_source_release(source)
  return name


def get_program_scene_name():
  return get_scene_name(obs.obs_frontend_get_current_scene())


def get_preview_scene_name():
  try:
    return get_scene_name(obs.obs_frontend_get_current_preview_scene())
  except Exception:
    return ""


def send_current_state():
  obs_settings_scenes_list = [
    obs_settings["scene_1"],
    obs_settings["scene_2"],
    obs_settings["scene_3"],
    obs_settings["scene_4"]
  ]
  mapping = {e: i+1 for i, e in enumerate(obs_settings_scenes_list) if e}

  program_id = mapping.get(get_program_scene_name(), 0)
  preview_id = mapping.get(get_preview_scene_name(), 0)
  brightness = str(obs_settings["brightness"]).zfill(3)
  
  payload = f"OTLCMD;{program_id};{preview_id};{brightness}\n"
  send_serial(payload)


def reconnect_serial():
  close_serial()
  open_serial()


def open_serial():
  global serial_conn, status_text

  if serial is None:
    status_text = "ERROR: pyserial not installed"
    obs.script_log(obs.LOG_INFO, status_text)
    return

  port = obs_settings["serial_port"]
  baudrate = obs_settings["baudrate"]

  if not port:
    status_text = "Not connected: no port selected"
    obs.script_log(obs.LOG_INFO, status_text)
    return

  try:
    serial_conn = serial.Serial(
      port=port,
      baudrate=baudrate,
      timeout=0,
      write_timeout=0
    )

    status_text = f"Connected: {port} @ {baudrate}"
    obs.script_log(obs.LOG_INFO, status_text)

  except Exception as e:
    serial_conn = None
    status_text = f"ERROR: {e}"
    obs.script_log(obs.LOG_INFO, status_text)


def close_serial():
  global serial_conn, status_text

  if serial_conn is not None:
    try:
      serial_conn.close()
    except Exception:
      pass

  serial_conn = None

  if obs_settings["serial_port"]:
    status_text = "Not connected"
    obs.script_log(obs.LOG_INFO, status_text)


def send_serial(payload):
  global serial_conn, status_text

  if serial_conn is None or not serial_conn.is_open:
    open_serial()

  if serial_conn is None:
    return

  try:
    serial_conn.reset_output_buffer()
    serial_conn.write(payload.encode("ascii"))
    serial_conn.flush()
    obs.script_log(obs.LOG_INFO, f"Sent command: {payload.strip()}")
  except Exception as e:
    status_text = f"ERROR: {e}"
    obs.script_log(obs.LOG_INFO, status_text)
    close_serial()