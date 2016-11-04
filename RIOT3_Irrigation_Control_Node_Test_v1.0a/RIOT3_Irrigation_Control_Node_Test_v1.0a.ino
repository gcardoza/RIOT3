//  Project:  RIOT3 - Remote IOT Node for controlling an 8 zone irrigation system
//  Author:   Geofrey Cardoza
//  Baseline: October 31st, 2016
//  Revision: November 3rd, 2016 - 
//
//  Hardware Configuration:
//    AdaFruit Feather Huzzah with ESP8266 Micro-controller
//      - WiFi & MQTT messaging interface
//    Sainsmart 8 relay module

  const char* Code_Version = " 1.0a";

// ***** Include header files *****
  #include "RioT_Test.h"            // RioT & BioT Security Data
  #include <PubSubClient.h>         // Library for MQTT Pub/Sub functions
  #include <ESP8266WiFi.h>          // Library for ESP8266 WiFi microcontroller
  
// ***** Declare system global variables
  const char* Node_Type = "RIOT3";
  char Node_Id[30];
  int Update_Interval = 60;          // set default Update interval to 60 sec. (can be changed)
  unsigned long Update_Sequence = 0; // Update sequence number to base
  char Irrigation_Status[150];       // Buffer to hold formatted sensor data payload

  // ***** MQTT pub/sub service info *****
  const char* Irrigation_Status_Topic = "/RioT3/Status";
  const char* Control_Topic = "/Control/RioT";
  WiFiClient espClient;
  PubSubClient client(espClient);
  char Control_Command[10];        // Inbound subscribed control message command
  int  Control_Data;               // Inbound subscribed control message data

  // ***** Define variables to track how long the program has been running *****
  unsigned long Current_Time, Zone_Start_Time;
  unsigned long Last_Publish_Time = 0;
  
  // Irrigation Control Variables. Each array is indexed by the Zone number starting at 1 (0 is ignored)
  #define Max_Zones 8
  #define Zone_Bit0_Pin 2
  #define Zone_Bit1_Pin 4
  #define Zone_Bit2_Pin 5
  #define Zone_Bit3_Pin 12

  int Active_Zone          = 0;
  int Zone_On_Duration[]    = {0, 1800, 1800, 1800, 3600, 3600, 3600,    0,    0};

  // Irrigation Zone ON array. Indexed by Zone number starting at 1 (Zone 0 turns all to OFF)
  int Zone_Bit0_Control[] = {1,0,1,0,1,0,1,0,1};
  int Zone_Bit1_Control[] = {1,0,0,1,1,0,0,1,1};
  int Zone_Bit2_Control[] = {1,0,0,0,0,1,1,1,1};
  int Zone_Bit3_Control[] = {1,0,0,0,0,0,0,0,0};

// ********** INITIALIZE ALL COMPONENTS OF THE SYSTEM **********
void setup()
{
  // ***** Start the serial port for debugging vi Arduino Serial Monitor *****
  Serial.begin(115200);
  delay(1000); //Pause to allow serial port to initialize
  
  Serial.print("\n***** STARTING RIOT3 - Code Version: ");
  Serial.print(Code_Version);
  Serial.println(" *****");
  Serial.println("\n***** ENTERING SETUP MODE *****");

  // ***** Start WiFi Communication Subsystem *****
  Serial.print("-> Connecting to WiFi Network\n");
  setup_wifi();

  // ***** Configure & Start MQTT Messaging service *****
  Serial.println("-> MQTT: Configuring Messaging Service");
  client.setServer(MQTT_Server, 1883); // Connect to MQTT Server
  Serial.print("  -> Server Address: ");
  Serial.println(MQTT_Server);
  client.setCallback(callback);        // Set the callback function when subscribed message arrives 
  client.subscribe(Control_Topic);     // Subscribe to Control Topic

  // ***** Configure Onboard LED (Pin 2) and set to Off *****
  Serial.print("-> LED: Configuring Onboard LED and set to off. GPIO PIN:");
  Serial.println(BUILTIN_LED);
  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  
  // ***** Initialize Zone control GPIO pins *****
  Initialize_GPIO_Ports();
  
  //Force an Irrigation Status Message on the first loop
  Last_Publish_Time = millis()/1000 - Update_Interval;
}


// ********** MAIN PROGRAM LOOP **********
void loop()
{
  // ***** Check for and Process Subscribed Messages *****
  client.loop();
  
  Current_Time = millis()/1000;    // get current program run-time in seconds
  // Serial.print("Current Time: ");
  // Serial.println(Current_Time);
  
  // ***** Manage Zone Schedule
  Process_Zone_Schedule();
  
  // ***** Send Irrigation Status Every Update_Interval
  Publish_Irrigation_Status();

  // delay(250);
}


// ===========================  Subroutines ===========================

// ***** Connect to the WiFi Network and establish Node Name *****
void setup_wifi()
{
  uint8_t mac[6];
    
  delay(10);
  // Connect to the WiFi network
  Serial.print("  -> Connecting to ");
  Serial.print(ssid);
  Serial.print(" ");
 
  WiFi.begin(ssid, WiFi_Password);

  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("Connected");
  
  // Create Node ID = Node_Type-MAC Address
  WiFi.macAddress(mac);
  sprintf(Node_Id, "%5s-%02x:%02x:%02x:%02x:%02x:%02x",Node_Type, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

  Serial.print("  -> Node IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("  -> Node ID: ");
  Serial.println(Node_Id);
}

// ***** Reconnect to MQTT service and subscribe to Control Topic *****
void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("\n  -> Attempting MQTT connection with ID: ");
    Serial.print(MQTT_Id);
    Serial.print(", PW:************");
    
    // Attempt to connect to the MQTT server
    if (client.connect(Node_Id, MQTT_Id, MQTT_Pw)) 
    {
      Serial.println(" ... connected");
      
      // Resubscribe to the MQTT Configuration topic
      Serial.print("  -> Subscribing to MQTT Control Topic service\n");
      client.subscribe(Control_Topic);
    } 
    else 
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// ***** Process Subscribed message received from BIOT *****
void callback(char* topic, byte* payload, unsigned int length)
{  
  char data_b[100], Control_Message[150];
  int i, x, y;
  
  // Flash LED for 0.5 seconds to indicate message arrival - GPIO 0
  digitalWrite(BUILTIN_LED, LOW);   // Note LOW turns LED on
  delay(500);
  digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off  
  
  Serial.println("\n***** Subscribed Topic Message Arrived *****");
  Serial.print ("-> Topic: ");
  Serial.print(topic);
  Serial.print(", Message Length: ");
  Serial.println(length);
  
  // Ensure the message is long enough to contain a valid command
  if(length <3) 
  {
    Serial.print("-> Control Message too short. Ignoring Command");
    return;
  }
  
  // Convert Payload to a character array
  for (int i = 0; i < length; i++) Control_Message[i] = (char)payload[i];
  Control_Message[length] = '\0'; // Ensure there is a string terminator
  Serial.print("-> Message: <");
  Serial.print(Control_Message);
  Serial.println(">");

  // ***** Process Inbound Command *****

  // Isolate the Control Command (first 3 characters)
  Serial.print("-> Processing Inbound Control Command: ");
  strncpy(Control_Command, Control_Message, 3);
  Control_Command[3] = '\0';
  Serial.println(Control_Command);

  // ** 1. Change Status Update Interval **
  if(!strcmp(Control_Command, "UF:"))
  {
    //Ensure Control_Message length is correct for this message
    if(length < 7) 
    {
      Serial.println("   -> UF: Message is too short. Ignoring command.");
      return;
    }
    strncpy(data_b, &Control_Message[3] ,4); 
    data_b[4] = '\0';
    Control_Data = atoi(data_b);
    
    Serial.print("   -> Changing Update Interval to ");
    Serial.print(Control_Data);
    Serial.println(" seconds");
    if(Control_Data < 5)
    {
      Serial.print ("      -> Value < 5 seconds. Forcing Interval to 5 seconds\n");
      Control_Data = 5;
    }
    Update_Interval = Control_Data;    // Set Sensor Data Update Interval
    return;
  }

  // ** 2. Force Node to Publish Sensor Data NOW **
  if(!strcmp(Control_Command, "UN:"))
  {
    Serial.print("   -> Forcing Node to Publish Status Now");
    Last_Publish_Time = millis()/1000 - Update_Interval;
    return;
  }
  
  // ** 3. Force Node to Reset the Update Sequence number to 0 **
  if(!strcmp(Control_Command, "RS:"))
  {
    Serial.println("   -> Forcing Node to Reset Sequence Number to 0");
    Update_Sequence = 0;
    Serial.println("   -> Forcing Node to Publish Sensor Data NOW");
    Last_Publish_Time = millis()/1000 - Update_Interval;
    return;
  }

  // ** 4. Stop all Zones **
  if(!strcmp(Control_Command, "OF:"))
  {
    // Turn all Zones Off - Done by turning zone 0 On
    Serial.println("   -> Stopping current schedule by setting Active Zone to 0");
    Turn_Zone_On(0);
    return;
  }

  // ** 5. Set OnTime for all Zones and start Irrigation Schedules **
  if(!strcmp(Control_Command, "SA:"))
  {
    //Ensure Control_Message length is correct for this message
    if(length < 8) 
    {
      Serial.println("   -> SA: Message is too short. Ignoring command.");
      return;
    }
    strncpy(data_b, &Control_Message[3] ,5); 
    data_b[5] = '\0';
    Control_Data = atoi(data_b);

    if(Control_Data == 0)
    {
      Serial.println("   -> Duration = 0. All Zones are being turned off");
      Turn_Zone_On(0);
      return;
    }
    
    Serial.print("   -> Starting all Zones Sequentially for ");
    Serial.print(Control_Data);
    Serial.println(" seconds");
    
    // Set Zone On Duration for all zones
    for(x=1; x <= Max_Zones; x++) Zone_On_Duration[x] = Control_Data;
    
    Turn_Zone_On(1);  //Start schedule with the first Zone
    return;
  }

  // ** 6. Set Irrigation Control Schedule **
  if(!strcmp(Control_Command, "IC:"))
  {
    //Ensure Control_Message length is correct for this message
    if(length < 75) 
    {
      Serial.println("   -> IC: Message is too short. Ignoring command.");
      return;
    }

    Serial.print("   -> Setting Irrigation Control Schedule");

    // Update Zone Schedule from COntrol Message
    y = 7;    // set index for Zone 1 On-Time
    for(x=1; x <= Max_Zones; x++)
    {
      strncpy(data_b, &Control_Message[y] ,5); 
      data_b[5] = '\0';
      Control_Data = atoi(data_b);
      Zone_On_Duration[x] = Control_Data;
      Serial.print("     -> Zone:");
      Serial.print(x);
      Serial.print(", On-Time:");
      Serial.println(Control_Data);
      y += 9;
    }
    // Start the Schedule
    Turn_Zone_On(1);  //Start schedule with the first Zone

    return;
  }
}

// ***** Format RIOT3 Irrigation Status message for BIOT2 Base Station *****
void Publish_Irrigation_Status()
{
  // ***** Define variables *****
  char SE_b[10], ZN_b[10];

  // Check if it is time to Send a Status message to the BioT
  if(Current_Time < Last_Publish_Time + Update_Interval) return; // Not Yet
  
  Serial.println("\n***** PREPARING TO PUBLISH IRRIGATION STATUS *****");
  Last_Publish_Time = Current_Time;   // Reset last publish time
  
  // Convert floating point vars into character strings
  dtostrf(Update_Sequence, 6, 0, SE_b);
 
  strcpy(Irrigation_Status, "NI:\0");
  strncat(Irrigation_Status, Node_Id, 23);
  strcat(Irrigation_Status, ",SW:");
  strncat(Irrigation_Status, Code_Version, 5);
  strcat(Irrigation_Status, ",SE:");
  strncat(Irrigation_Status, SE_b, 6);
  strcat(Irrigation_Status, ",ST:");
  sprintf(ZN_b, ",Z%1i:%5i\0", Active_Zone, Zone_On_Duration[Active_Zone]);
  strcat(Irrigation_Status, ZN_b);

  // Ensure a connection exists with the MQTT server on the Pi
  Serial.print("-> Checking connection to MQTT server...");
  if (!client.connected()) reconnect();
  else Serial.println("connected");
  
  // Publish Sensor Data to MQTT message queue
  Serial.print("-> Sending Status to MQTT Topic:");
  Serial.println(Irrigation_Status_Topic);
  Serial.println(Irrigation_Status);
  
  client.publish(Irrigation_Status_Topic, Irrigation_Status);
  
  // Update Record Sequence
  Update_Sequence++;
  if(Update_Sequence > 999999)
    Update_Sequence = 0; 
}

// ***** Initialize Zone Control GPIO Ports *****
void Initialize_GPIO_Ports()
{
  // ***** Configure Zone GPIO ports as OUTPUTs and Set to HIGH for OFF*****
  Serial.println("   -> Initializing Zone Control GPIO Pins as Outputs");

  pinMode(Zone_Bit0_Pin, OUTPUT);
  pinMode(Zone_Bit1_Pin, OUTPUT);
  pinMode(Zone_Bit2_Pin, OUTPUT);
  pinMode(Zone_Bit3_Pin, OUTPUT);

  // Turn all Zones Off - Done by turning zone 0 On
  Turn_Zone_On(0);
}

// ***** Turn the specifid Zone, 0 = All Zones off *****
void Turn_Zone_On(int zone)
{
  Active_Zone = zone;               // Set new active zone
  Zone_Start_Time = millis()/1000;  // Set new zone start time
  
  if (zone == 0) Serial.println("   -> Turning off ALL Zones");
  else
  {
    Serial.print("   -> Turning on Zone: ");
    Serial.print(zone);
    Serial.print(", Start Time: ");
    Serial.println(Zone_Start_Time);
  }
  
  // Turn on this zone
  digitalWrite(Zone_Bit0_Pin, Zone_Bit0_Control[zone]);
  digitalWrite(Zone_Bit1_Pin, Zone_Bit1_Control[zone]);
  digitalWrite(Zone_Bit2_Pin, Zone_Bit2_Control[zone]);
  digitalWrite(Zone_Bit3_Pin, Zone_Bit3_Control[zone]);

  // Send a status message to BioT
  Serial.print("   -> Forcing Node to Publish Status Now");
  Last_Publish_Time = millis()/1000 - Update_Interval;
}

// ***** Process the current Zone Schedule
void Process_Zone_Schedule()
{
  int x, prev_zone;
  // ** Check if a schedule is active
  if(Active_Zone == 0) return;  //Return if no zone is on

  // ** Check if current zone on-time has expired
  if(Current_Time < Zone_Start_Time + Zone_On_Duration[Active_Zone]) return; //It hasn't
  
  // Current Zone has expired, turn it off and find next scheduled zone
  Zone_On_Duration[Active_Zone] = 0;  // Clear Current Zones' Schedule
  prev_zone = Active_Zone;            // Store Active Zone to help find next one
  Turn_Zone_On(0);

  for(x += prev_zone; x <= Max_Zones; x++)
  {
    if(Zone_On_Duration[x] != 0)
    {
      Turn_Zone_On(x);      // Start active zone
      return;
    }
  }
}
