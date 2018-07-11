/*
  xdrv_19_Shutter.ino - PWM, WS2812 and sonoff led support for Sonoff-TasmotaS

  Copyright (C) 2018  Stefan Bode

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

enum ShutterCommands {
  CMND_OPEN, CMND_CLOSE, CMND_STOP, CMND_POSITION, CMND_OPENTIME, CMND_CLOSETIME, CMND_SHUTTERRELAY };
const char kShutterCommands[] PROGMEM =
  D_CMND_OPEN "|" D_CMND_CLOSE "|" D_CMND_STOP "|" D_CMND_POSITION  "|" D_CMND_OPENTIME "|" D_CMND_CLOSETIME "|" D_CMND_SHUTTERRELAY;


const char JSON_SHUTTER_POS[] PROGMEM = "%s,\"%s\":%d";                                  // {s} = <tr><th>, {m} = </th><td>, {e} = </td></tr>

uint8_t Shutter_Open_Time = 0;               // duration to open the shutter
uint8_t Shutter_Close_Time = 0;              // duratin to close the shutter
uint8_t Shutter_Open_Velocity = 100;
int32_t Shutter_Open_Max = 0;               // max value on maximum open calculated
int32_t Shutter_Target_Position = 0;        // position to go to
uint16_t Shutter_Close_Velocity =0;          // in relation to open velocity. higher value = faster
int8_t  Shutter_Direction = 0;               // 1 == UP , 0 == stop; -1 == down
int32_t Shutter_Real_Position = 0;          // value between 0 and Shutter_Open_Max
power_t shutter_mask = 0;

void ShutterInit()
{
  // set startrelay to 1 on first init
  Settings.shutter_startrelay = (Settings.shutter_startrelay = 0 ? 1 : Settings.shutter_startrelay);
  Shutter_Open_Time = (Settings.shutter_opentime>0 ? Settings.shutter_opentime : 10);
  Shutter_Close_Time = (Settings.shutter_closetime> 0 ? Settings.shutter_closetime : 10);
  // Update Calculation
  Shutter_Open_Max = 20 * Shutter_Open_Velocity * Shutter_Open_Time;
  Shutter_Close_Velocity = Shutter_Open_Max / ( Shutter_Close_Time * 20 );

  Shutter_Real_Position =   Settings.shutter_position * Shutter_Open_Max / 100;
  snprintf_P(log_data, sizeof(log_data), PSTR("ShutterInit: Position %d [%d %%], Open Velocity: %d Close Velocity: %d , MAx Way: %d, Opentime %d [s], Closetime %d [s]"), Shutter_Real_Position,Settings.shutter_position, Shutter_Open_Velocity, Shutter_Close_Velocity , Shutter_Open_Max, Shutter_Open_Time, Shutter_Close_Time);
  AddLog(LOG_LEVEL_INFO);
  shutter_mask = 3 << (Settings.shutter_startrelay -1);
}

void Schutter_Update_Position()
{
  if (Shutter_Direction != 0) {
    Shutter_Real_Position = Shutter_Real_Position + (Shutter_Direction > 0 ? Shutter_Open_Velocity : -Shutter_Close_Velocity);
    // check if corresponding relay if OFF. Then stop movement.
    //snprintf_P(log_data, sizeof(log_data), PSTR("Debug: Powerstate: %d, Source: %d"), ((1 << (Settings.shutter_startrelay + (Shutter_Direction == 1 ? -1 : 0))) & power), last_source);
    //AddLog(LOG_LEVEL_DEBUG);
    if ( (((1 << (Settings.shutter_startrelay + (Shutter_Direction == 1 ? -1 : 0))) & power) == 0)  && SRC_PULSETIMER != last_source) {
      Shutter_Target_Position = Shutter_Real_Position;
      snprintf_P(log_data, sizeof(log_data), PSTR("Switch OFF motors. Target: %ld"), Shutter_Target_Position);
      AddLog(LOG_LEVEL_DEBUG);
    }
    if ( (((1 << (Settings.shutter_startrelay + (Shutter_Direction == 1 ? -1 : 0))) & power) > 0)  && SRC_SHUTTER != last_source) {
      // someone else switches ON the relas during movement. Normally only possible during MOMENTARY Mode
      // shutter in movement. need to stop the Shutter
      Shutter_Target_Position = Shutter_Real_Position;
      snprintf_P(log_data, sizeof(log_data), PSTR("Switch momentary motors OFF. Target: %ld"), Shutter_Target_Position);
      AddLog(LOG_LEVEL_DEBUG);
    }
    if (Shutter_Real_Position * Shutter_Direction >= Shutter_Target_Position * Shutter_Direction || Shutter_Real_Position < Shutter_Close_Velocity) {
      snprintf_P(log_data, sizeof(log_data), PSTR("NEW Shutter: Stopped Position %d, relay: %d, pulsetimer: %d"), Shutter_Real_Position, (Settings.shutter_startrelay + (Shutter_Direction == 1 ? 0 : 1) -1), Settings.pulse_timer[(Settings.shutter_startrelay + (Shutter_Direction == 1 ? 0 : 1) -1)]);
      AddLog(LOG_LEVEL_DEBUG);
      if (Settings.pulse_timer[(Settings.shutter_startrelay + (Shutter_Direction == 1 ? 0 : 1) -1)] > 0) {
        // we have a momentary switch here
        if (SRC_PULSETIMER == last_source) {
          ExecuteCommandPower(Settings.shutter_startrelay + (Shutter_Direction == 1 ? 0 : 1), 1, SRC_SHUTTER);
        } else {
          last_source = SRC_SHUTTER;
        }
      } else {
        ExecuteCommandPower(Settings.shutter_startrelay + (Shutter_Direction == 1 ? 0 : 1), 0, SRC_SHUTTER);
      }
      Shutter_Direction=0;
      Settings.shutter_position = Shutter_Real_Position  *100 / Shutter_Open_Max;
    }
  } else {
    // no movement or manual movement
    if ((shutter_mask & power) > 0 && SRC_SHUTTER != last_source) { // ONE of the two relays from the shutter report ON and direction == 0 ==> manual movement request
      snprintf_P(log_data, sizeof(log_data), PSTR("Start moving shutter..."));
      AddLog(LOG_LEVEL_DEBUG);
      last_source = SRC_SHUTTER; // avoid switch off in the next loop
      if (((1 << Settings.shutter_startrelay ) & power) > 0) { // tetsing on CLOSE relay, if ON
        // close with relay two
        Shutter_Direction=-1;
        Shutter_Target_Position = 0;
      } else {
        // opens with relay one
        Shutter_Direction=1;
        Shutter_Target_Position = Shutter_Open_Max;
      }
      snprintf_P(log_data, sizeof(log_data), PSTR("Shutter is moving %ld, power: %ld, join: %ld, direction: %d"),shutter_mask ,power, shutter_mask & power, Shutter_Direction);
      AddLog(LOG_LEVEL_DEBUG);
    }
  }
}
/********************************************************************************************/

//boolean ShutterCommand(char *type, uint16_t index, char *dataBuf, uint16_t XdrvMailbox.data_len, int16_t XdrvMailbox.payload)
boolean ShutterCommand()
{
  char command [CMDSZ];
  boolean serviced = true;
  boolean valid_entry = false;

  snprintf_P(log_data, sizeof(log_data), PSTR("Shutter: Command Code: '%s', Payload: %d"), XdrvMailbox.topic, XdrvMailbox.payload);
  AddLog(LOG_LEVEL_DEBUG);
  int command_code = GetCommandCode(command, sizeof(command), XdrvMailbox.topic, kShutterCommands);
  if (-1 == command_code) {
    serviced = false;  // Unknown command
  }
  else if (CMND_OPEN == command_code) {
    Shutter_Direction = 1;
    Shutter_Target_Position = Shutter_Open_Max;
    ExecuteCommandPower(Settings.shutter_startrelay, 1, SRC_SHUTTER);
    snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_COMMAND_NVALUE, command, 1);
  }
  else if (CMND_CLOSE == command_code) {
    Shutter_Direction = -1;
    Shutter_Target_Position = 0;
    ExecuteCommandPower(Settings.shutter_startrelay+1, 1, SRC_SHUTTER);
    snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_COMMAND_NVALUE, command, 1);
  }
  else if (CMND_STOP == command_code) {
    Shutter_Target_Position = Shutter_Real_Position;
    snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_COMMAND_NVALUE, command, 1);
  }
  else if (CMND_POSITION == command_code) {
    if ( (XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 100)) {
      Shutter_Target_Position =  XdrvMailbox.payload * Shutter_Open_Max / 100;
      Shutter_Direction =  (Shutter_Real_Position < Shutter_Target_Position ? 1 : -1);
      ExecuteCommandPower(Settings.shutter_startrelay + (Shutter_Direction == 1 ? 0 : 1), 1, SRC_SHUTTER);
      snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_COMMAND_NVALUE, command, XdrvMailbox.payload);
    } else {
      snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_COMMAND_NVALUE, command, Settings.shutter_position);
    }
  }
  else if (((CMND_OPENTIME == command_code) || (CMND_CLOSETIME == command_code) ) ) {
    if (  (XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 100)) {
      if (CMND_OPENTIME == command_code) {
        Settings.shutter_opentime = XdrvMailbox.payload;
      } else {
        Settings.shutter_closetime = XdrvMailbox.payload;
      }
      snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_COMMAND_NVALUE, command,XdrvMailbox.payload);
      ShutterInit();
    } else {
        snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_COMMAND_NVALUE, command, (CMND_OPENTIME == command_code ? Settings.shutter_opentime : Settings.shutter_closetime));
    }
  }
  else if (CMND_SHUTTERRELAY == command_code) {
    if ( (XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 64)) {
      Settings.shutter_startrelay = XdrvMailbox.payload;
      shutter_mask = 3 << (Settings.shutter_startrelay -1);
    }
    snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_COMMAND_NVALUE, command, Settings.shutter_startrelay );
  } else {
    serviced = false;  // Unknown command
  }
  return serviced;
}

void Schutter_Report_Position()
{
  if (Shutter_Direction != 0) {
    snprintf_P(log_data, sizeof(log_data), PSTR("Shutter: Real Position %d, Target %d, Close Velocity %d"), Shutter_Real_Position, Shutter_Target_Position, Shutter_Close_Velocity);
    AddLog(LOG_LEVEL_DEBUG_MORE);
    //Settings.shutter_position = Shutter_Open_Max *100 / Shutter_Real_Position;
  }
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

#define XDRV_97

boolean Xdrv97(byte function)
{
  boolean result = false;

  if (Settings.flag3.shutter_mode) {
    switch (function) {
      case FUNC_PRE_INIT:
        ShutterInit();
        break;
      case FUNC_EVERY_50_MSECOND:
        Schutter_Update_Position();
        break;
      case FUNC_EVERY_SECOND:
        Schutter_Report_Position();
        break;
      case FUNC_COMMAND:
        result = ShutterCommand();
        break;
      case FUNC_JSON_APPEND:
        snprintf_P(mqtt_data, sizeof(mqtt_data), JSON_SHUTTER_POS, mqtt_data, D_SHUTTER, Shutter_Real_Position * 100 / Shutter_Open_Max);
        break;
    }
  }
  return result;
}
