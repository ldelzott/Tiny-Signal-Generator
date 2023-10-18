// TODO: Finishing sinus edit
// TODO: Adding Constant, Custome, Pure PWM editors
// TODO: Optional: Something could be wrong with the "selection" value of the "MenuScreen" struct: the value seems to be replaced by "-1" each time the menu is left.  
// TODO: Adding a bit of space between the "Return" item and the other items for aesthetic purpose.  
// TODO: Adding coarse/fine tuning in Sin editor
// TODO: Drawing the parametrized sine wave
// TODO: Adding pan view for the grid and the sine wave
// TODO: Avoiding overlapping of grid indications
// TODO: Adding value integrity check to avoid range overflow for specifics parameters
//        1. The MIN value for the HZOOM and VZOOM parameters of the sine waves should be set to 2 and 20. 
//        2. 
// TODO: Normalizing displayed barreA, barreB and cursor values by diving by "OPERATING_FREQUENCY"
// TODO: Scene time-length adjustements
// TODO: "Vertical" adjustement tool in "Nouveau Signal" menu works on the screen but does not seem to influence the port output.
// TODO: "Phase" adjustement tool in "Nouveau Signal" does seem to work for both the screen and the port output.


// The reported datatype memory size using Serial.println(sizeof([DATATYPE])) are as follow:
//    -> uint8_t        1 byte
//    -> uint16_t       2 bytes
//    -> int            4 bytes
//    -> unsigned int   4 bytes 
//    -> long           4 bytes
//    -> double         8 bytes

#include <FS.h>
#include "SPIFFS.h"
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#include <cstring>

#define BOUNCING_DELAY 150 // Milliseconds of delay between two valid push-button entries

#define BUTTON_NAVIGATION_PIN 4
#define BUTTON_SELECTION_PIN 23
#define BUTTON_MINUS_PIN 15
#define BUTTON_PLUS_PIN 14

#define SIGNAL_NUMBER 16
#define SIGNAL_RESOLUTION 2048
#define SIGNAL_DEPTH_RESOLUTION 4095 // 12 bits
#define OPERATING_FREQUENCY 50
#define MAX_SYSTEM_VOLTAGE 5

#define DATATYPE_BYTESIZE 2
#define DATATYPE_UINT16_RANGE 65535

#define SIN_GRID_WIDTH 206 // Should be strictly less than 240
#define SIN_GRID_HEIGHT 100 // Should be strictly less than 240
#define SIN_GRID_X_OFFSET 25
#define SIN_GRID_Y_OFFSET 100
#define SIN_GRID_X_TEXT_SPACING 20
#define SIN_GRID_X_STEP_MIN 4
#define SIN_GRID_MAX_X_TRAVELLING_OFFSET 2048
#define SIN_GRID_MIN_X_TRAVELLING_OFFSET 0
#define SIN_DISTINCT_INCREMENT_VALUES 6 // Changing the "button_step_increment_values" array size accordingly when updating this value
#define SIN_INITIAL_INCREMENT_INDEX 2 // Should be strictly less than SIN_DISTINCT_INCREMENT_VALUES

#define LED_DEVICE_SELECTION 0
#define MOTOR_DEVICE_SELECTION 1
#define PWM_DUTY_RANGE 4094
#define PWM_PCA9685_OPERATING_FREQUENCY 1000
#define PWM_PCA9685_OPERATING_FREQUENCY_MOTOR 60 // Assuming 50Hz servo motors
#define SERVO_DEFAULT_MIN_DUTY 125
#define SERVO_DEFAULT_MAX_DUTY 575

/////////////////////// STRUCTS

struct PushButton {
  const uint8_t PIN;
  const int debounceDelay;
  unsigned long lastPress;
  bool state;
};

struct MenuScreen {
  char menuTitle[20];
  int selection;
  int selectionMax;
  bool hasReturnOption;
  int itemNumber;
  const char* itemList[17];
  int itemFontSize;
  int itemSpatialOffset;
  int lastItemOffset;
  bool isWelcomeScreen;
};

struct Signal {
  float amplitude;
  float frequency;
  float phase;
  float y_offset;
};



/////////////////////// BUTTON ISRs

PushButton button_navigation = {BUTTON_NAVIGATION_PIN, BOUNCING_DELAY, millis() + BOUNCING_DELAY, false};
PushButton button_selection = {BUTTON_SELECTION_PIN, BOUNCING_DELAY, millis() + BOUNCING_DELAY, false};
PushButton button_minus = {BUTTON_MINUS_PIN, BOUNCING_DELAY, millis() + BOUNCING_DELAY, false};
PushButton button_plus = {BUTTON_PLUS_PIN, BOUNCING_DELAY, millis() + BOUNCING_DELAY, false};

void IRAM_ATTR isr_navigation_button() {
  if ((millis() - button_navigation.lastPress) > button_navigation.debounceDelay && button_navigation.state == false) {
    button_navigation.state = true; button_navigation.lastPress = millis();
  }
}

void IRAM_ATTR isr_selection_button() {
  if ((millis() - button_selection.lastPress) > button_selection.debounceDelay && button_selection.state == false) {
    button_selection.state = true; button_selection.lastPress = millis();
  }
}

void IRAM_ATTR isr_minus_button() {
  if ((millis() - button_minus.lastPress) > button_minus.debounceDelay && button_minus.state == false) {
    button_minus.state = true; button_minus.lastPress = millis();
  }
}

void IRAM_ATTR isr_plus_button() {
  if ((millis() - button_plus.lastPress) > button_plus.debounceDelay && button_plus.state == false) {
    button_plus.state = true; button_plus.lastPress = millis();
  }
}


///////////////////// INITIALIZATION AND MAIN LOOP

MenuScreen welcomeScreen = {"Menu Principal", 
                            -1, 
                             2, 
                             false, 
                             2, 
                             {"Editer scene", "Visualiser scene"}, 
                             2,
                             110,
                             0, 
                             true};
                             
MenuScreen signalSelectionScreen = {"Selectionner port", 
                                    -1, 
                                    17, 
                                    true, 
                                    17, 
                                    {"Port 1", "Port 2", "Port 3", "Port 4", "Port 5","Port 6", "Port 7", "Port 8", "Port 9", "Port 10", "Port 11","Port 12", "Port 13", "Port 14", "Port 15", "Port 16", "Retour"},
                                    1, 
                                    40,
                                    15, 
                                    false};
                                    
MenuScreen formeSelectionScreen = {"Selectionner forme", 
                                    -1, 
                                    3, 
                                    true, 
                                    3, 
                                    {"Nouveau signal", "Modifier", "Retour"},
                                    2, 
                                    100,
                                    15, 
                                    false};

MenuScreen currentMenuScreen;

int screenID;
int selectedSignal;
int selectedShape;
int selectedShapes[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};


// 16xSIGNAL_RESOLUTIONx2 bytes gives:
//     -> 16x1024x2 bytes = 32768 bytes   = 32,7 Kbytes
//     -> 16x2048x2 bytes = 65535 bytes   = 65,5 Kbytes
//     -> 16x4096x2 bytes = 131072 bytes  = 131 Kbytes

uint16_t master_signal[SIGNAL_NUMBER][SIGNAL_RESOLUTION]; 
uint16_t master_signal_buffer[SIGNAL_RESOLUTION]; // Used when customizing a master_signal

float actual_button_step_increment_value;
int button_step_index=SIN_INITIAL_INCREMENT_INDEX;
float button_step_increment_values[SIN_DISTINCT_INCREMENT_VALUES]={0.01,0.1,1,10,100,1000};


int sin_selectedParameter;
String sin_signalParamsNames[] = {"Amplitude", "Frequence", "Phase", "Vertical", "H. ZOOM", "Decalage", "Retour"};
int sin_numberOfParams = 6;
float sin_signalParams[16][7] = { {2.5, 1.0, 1, 0, 20, 0, 20}, 
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20},
                                  {2.5, 1.0, 1, 0, 20, 0, 20}};

int viz_selectedParameter;
String viz_signalParamsNames[] = {"H. ZOOM", "Decalage", "Signal", "Retour"};
int viz_numberOfParams = 3;
float viz_signalParams[4] = {20, 0, 0, 20};

int constant_device_selection;
int constant_selectedParameter;
String constant_signalParamsNames[] = {"H. ZOOM", "Decalage", "Barre A", "Barre B", "Curseur", "Gain", "Puissance", "Duree T", "Composant", "Retour"}; // Do not change the "Composant" position in the array. 
int constant_numberOfParams = 9;
float constant_signalParams[16][9] = {{100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION}, 
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION},
                                      {100, 0, 10, 40, 25, 1, 2, OPERATING_FREQUENCY*2, LED_DEVICE_SELECTION}};



int PWM_frequency_toggle = 0;

TFT_eSPI tft = TFT_eSPI();
Adafruit_PWMServoDriver PCA9685 = Adafruit_PWMServoDriver(0x40, Wire);

void flash_data_loading_screen(){
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);             // set text size to 2
  tft.setCursor(12, tft.height()/2-50);            // set cursor position
  tft.setTextColor(TFT_WHITE);
  tft.println("Lecture memoire...");           // print "Menu" at the top of the screen
  tft.drawLine(tft.width(), tft.height()/2.0-15-40, 0, tft.height()/2.0-15-40, TFT_WHITE);
  tft.drawLine(tft.width(), tft.height()/2.0+10-40, 0, tft.height()/2.0+10-40, TFT_WHITE);
}

void flash_data_writting_screen(){
  tft.fillScreen(TFT_BLACK);
  //tft.fillRect( 0,tft.height()/2.0-15, tft.width(), 25,TFT_BLACK);
  tft.setTextSize(2);             // set text size to 2
  tft.setCursor(12, tft.height()/2-50);            // set cursor position
  tft.setTextColor(TFT_WHITE);
  tft.println("Enregistrement...");           // print "Menu" at the top of the screen
  tft.drawLine(tft.width(), tft.height()/2.0-15-40, 0, tft.height()/2.0-15-40, TFT_WHITE);
  tft.drawLine(tft.width(), tft.height()/2.0+10-40, 0, tft.height()/2.0+10-40, TFT_WHITE);
}

void retrieve_data_from_flash(){
  flash_data_loading_screen();
  if(!SPIFFS.begin(true)){ // Argument should be false but works anyway... 
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  File opened_file = SPIFFS.open("/masterSignal.bin", "r");
  if (!opened_file) {
    Serial.println("An Error has occurred while opening file for reading");
    return;
  }
  opened_file.read((uint8_t*)master_signal, sizeof(master_signal));
  opened_file.close();
  SPIFFS.end();
}


void write_data_to_flash(){
  flash_data_writting_screen();
  if(!SPIFFS.begin(true)){ // Argument should be false but works anyway... 
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  
  File file = SPIFFS.open("/masterSignal.bin", "w");
  if (!file) {
    Serial.println("An Error has occurred while opening file for writing");
    return;
  }
  file.write((uint8_t*)master_signal, sizeof(master_signal));
  file.close();
  SPIFFS.end();
}

void resetButtons(){
  button_selection.state = false;
  button_navigation.state = false;
  button_plus.state = false;
  button_minus.state = false;
  tft.fillScreen(TFT_BLACK);
}


void drawMenuScreen(){
  int lastItemOffset = 0;
  tft.setTextSize(2);             // set text size to 2
  tft.setCursor(0, 0);            // set cursor position
  tft.setTextColor(TFT_WHITE);
  tft.println(currentMenuScreen.menuTitle);           // print "Menu" at the top of the screen
  tft.drawLine(tft.width(), 20, 0, 20, TFT_BLUE);
  tft.drawLine(tft.width(), 21, 0, 21, TFT_BLUE);
  tft.setTextSize(currentMenuScreen.itemFontSize); 
  for (int i = 0; i < currentMenuScreen.selectionMax; i++) {
    if( i == currentMenuScreen.selectionMax-1){
      lastItemOffset = currentMenuScreen.lastItemOffset;
    }
    tft.setCursor(0, currentMenuScreen.itemSpatialOffset + i*10*currentMenuScreen.itemFontSize + lastItemOffset);
    if (i == currentMenuScreen.selection) {
      tft.setTextColor(TFT_BLUE);
    } else {
      tft.setTextColor(TFT_WHITE);
    }
    tft.println(currentMenuScreen.itemList[i]);
  }
  if (currentMenuScreen.isWelcomeScreen){
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(200, 220);
    tft.setTextSize(1);
    tft.println("v1.0");
  }
  tft.setTextColor(TFT_WHITE);
}



void new_sinewave_initialization(){
  
  float phase = sin_signalParams[selectedSignal][2] * OPERATING_FREQUENCY; // Allows to define the phase in seconds instead of graph points 
  float vertical_shift = (-1) * sin_signalParams[selectedSignal][3] * (float)(DATATYPE_UINT16_RANGE/MAX_SYSTEM_VOLTAGE); // Volt-wise steps
  float frequency = 2*PI*sin_signalParams[selectedSignal][1] / OPERATING_FREQUENCY; // A sine frequency of one Hz is made of OPERATING_FREQUENCY discrete points since the since the system runs at OPERATING_FREQUENCY Hz.      
  float amplitude = (sin_signalParams[selectedSignal][0]*2) / MAX_SYSTEM_VOLTAGE; // The x2 account for the amplitude measure of the sine wave (i.e half of the total vertical span of the signal) 
 
  
  for(int i=0; i<SIGNAL_RESOLUTION; i++){
    // The sine wave has a vertical offset since the datatype uint16_t does not seem to handle signed numbers.
    // To avoid loosing in height resolution at this stage of the code, the provided signal should fill the 65535 values of uint16_t datatype
    int sin_value_without_overflow = ( amplitude * (float)(DATATYPE_UINT16_RANGE/2) * (1+sin((i+phase)*frequency)) - vertical_shift );
    if(sin_value_without_overflow>=DATATYPE_UINT16_RANGE-1){
      master_signal[selectedSignal][i] = DATATYPE_UINT16_RANGE - 1;
    } else if (sin_value_without_overflow<=0){
      master_signal[selectedSignal][i] = 0;
    } else{
      master_signal[selectedSignal][i] = (uint16_t)sin_value_without_overflow;
    }
    //Serial.println(master_signal[selectedSignal][i]);
  }
}

void drawNewSignal(){
  
  float number_of_fetched_points = ((float)SIN_GRID_WIDTH/sin_signalParams[selectedSignal][4])*(float)OPERATING_FREQUENCY;
  float pixel_distance = SIN_GRID_WIDTH/number_of_fetched_points;
  float index_value = sin_signalParams[selectedSignal][5] / pixel_distance;
  
  for(float i=0; i<number_of_fetched_points; i++){
    index_value++;
    if ( index_value < SIGNAL_RESOLUTION ){
      int normalized_to_screen = int( ( (DATATYPE_UINT16_RANGE-(float)master_signal[selectedSignal][(int)index_value]) / (float)DATATYPE_UINT16_RANGE ) * (float)SIN_GRID_HEIGHT );
      int center_graph_offset = 0;// int((MAX_SYSTEM_VOLTAGE - (float)sin_signalParams[selectedSignal][0]*2)/MAX_SYSTEM_VOLTAGE * (float)SIN_GRID_HEIGHT)/2;
      tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance), SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen, TFT_RED);
      tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance), SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen+1, TFT_RED); // Double the pixel height of the line to make it more visible on the screen
      tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance)+1, SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen, TFT_RED); // Double the pixel width of the line to make it more visible on the screen
    }
  } 
 
}

void drawNewSignal_viz(){
  selectedSignal = int(viz_signalParams[2]);
  float number_of_fetched_points = ((float)SIN_GRID_WIDTH/viz_signalParams[0])*(float)OPERATING_FREQUENCY;
  float pixel_distance = SIN_GRID_WIDTH/number_of_fetched_points;
  float index_value = viz_signalParams[1] / pixel_distance;
  
  for(float i=0; i<number_of_fetched_points; i++){
    index_value++;
    if ( index_value < SIGNAL_RESOLUTION ){
      int normalized_to_screen = int( ( (DATATYPE_UINT16_RANGE-(float)master_signal[selectedSignal][(int)index_value]) / (float)DATATYPE_UINT16_RANGE ) * (float)SIN_GRID_HEIGHT );
      int center_graph_offset = 0;// int((MAX_SYSTEM_VOLTAGE - (float)sin_signalParams[selectedSignal][0]*2)/MAX_SYSTEM_VOLTAGE * (float)SIN_GRID_HEIGHT)/2;
      tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance), SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen, TFT_RED);
      tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance), SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen+1, TFT_RED); // Double the pixel height of the line to make it more visible on the screen
      tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance)+1, SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen, TFT_RED); // Double the pixel width of the line to make it more visible on the screen
    }
  } 
 
}

void drawSinGrid(){ 
  
  tft.setTextColor(TFT_WHITE);
  
  int gridStep_x = int(sin_signalParams[selectedSignal][4]);
  int gridStep_y = 20 ; // int(sin_signalParams[selectedSignal][6]);
  int gridStep_x_travelling_offset = int(sin_signalParams[selectedSignal][5]);
  int voltage_division=0;
  int time_division=0;
  int delta_x = SIN_GRID_X_TEXT_SPACING; // Init value to see the "0" marking
  int x_old = 0;

  tft.setTextSize(1);             // set text size to 2
  tft.setCursor(SIN_GRID_X_OFFSET+3, SIN_GRID_Y_OFFSET + int(SIN_GRID_HEIGHT/2) - 25);
  tft.println(""); tft.println("V");  tft.println("O"); tft.println("L"); tft.println("T"); 
  tft.setCursor(SIN_GRID_X_OFFSET + int(SIN_GRID_WIDTH/2) - 25, SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT + 27);
  tft.println("SECONDE");
  tft.setCursor(SIN_GRID_X_OFFSET + SIN_GRID_WIDTH-55, SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT + 27);
  tft.setTextColor(TFT_RED);
  tft.println("+-"+String(actual_button_step_increment_value));
  tft.setTextColor(TFT_WHITE);
  
  // Draw the vertical lines
  for (int x = -gridStep_x_travelling_offset; x <= SIN_GRID_WIDTH+gridStep_x_travelling_offset; x += gridStep_x) {
    if (x<=SIN_GRID_X_OFFSET-30){
      time_division++;
      continue;
    }
    if (x>=SIN_GRID_X_OFFSET+SIN_GRID_WIDTH-20){
      time_division++;
      continue;
    }
    delta_x = delta_x + (x-x_old); // Managing the spacing between the x grid annotations. 
    if (delta_x >= SIN_GRID_X_TEXT_SPACING){
      tft.setCursor(x+SIN_GRID_X_OFFSET-3, SIN_GRID_HEIGHT+SIN_GRID_Y_OFFSET+9);
      tft.print(String(time_division));
      delta_x=0;
      tft.drawLine(x+SIN_GRID_X_OFFSET, SIN_GRID_Y_OFFSET, x+SIN_GRID_X_OFFSET, SIN_GRID_HEIGHT+SIN_GRID_Y_OFFSET+3, TFT_WHITE); // The "3" value here serves a as "mark" for the corresponding text annotation. 
    } else {
      tft.drawLine(x+SIN_GRID_X_OFFSET, SIN_GRID_Y_OFFSET, x+SIN_GRID_X_OFFSET, SIN_GRID_HEIGHT+SIN_GRID_Y_OFFSET, TFT_WHITE);
    }
    x_old = x;
    time_division++;
  }

  // Draw the horizontal lines
  for (int y = SIN_GRID_HEIGHT; y >= 0; y -= gridStep_y) {
    tft.drawLine(SIN_GRID_X_OFFSET-3, y+SIN_GRID_Y_OFFSET, SIN_GRID_WIDTH + SIN_GRID_X_OFFSET+3, y+SIN_GRID_Y_OFFSET, TFT_WHITE);
    tft.setCursor(SIN_GRID_X_OFFSET-13, y+SIN_GRID_Y_OFFSET-3);
    tft.print(String(voltage_division));
    voltage_division++;
  }
  
  new_sinewave_initialization();
  drawNewSignal();

}


void drawSinGrid_viz(){ 
  
  tft.setTextColor(TFT_WHITE);
  
  int gridStep_x = int(viz_signalParams[0]);
  int gridStep_y = 20 ; // int(sin_signalParams[selectedSignal][6]);
  int gridStep_x_travelling_offset = int(viz_signalParams[1]);
  int voltage_division=0;
  int time_division=0;
  int delta_x = SIN_GRID_X_TEXT_SPACING; // Init value to see the "0" marking
  int x_old = 0;

  tft.setTextSize(1);             // set text size to 2
  tft.setCursor(SIN_GRID_X_OFFSET+3, SIN_GRID_Y_OFFSET + int(SIN_GRID_HEIGHT/2) - 25);
  tft.println(""); tft.println("V");  tft.println("O"); tft.println("L"); tft.println("T"); 
  tft.setCursor(SIN_GRID_X_OFFSET + int(SIN_GRID_WIDTH/2) - 25, SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT + 27);
  tft.println("SECONDE");
  tft.setCursor(SIN_GRID_X_OFFSET + SIN_GRID_WIDTH-55, SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT + 27);
  tft.setTextColor(TFT_RED);
  tft.println("+-"+String(actual_button_step_increment_value));
  tft.setTextColor(TFT_WHITE);
  
  // Draw the vertical lines
  for (int x = -gridStep_x_travelling_offset; x <= SIN_GRID_WIDTH+gridStep_x_travelling_offset; x += gridStep_x) {
    if (x<=SIN_GRID_X_OFFSET-30){
      time_division++;
      continue;
    }
    if (x>=SIN_GRID_X_OFFSET+SIN_GRID_WIDTH-20){
      time_division++;
      continue;
    }
    delta_x = delta_x + (x-x_old); // Managing the spacing between the x grid annotations. 
    if (delta_x >= SIN_GRID_X_TEXT_SPACING){
      tft.setCursor(x+SIN_GRID_X_OFFSET-3, SIN_GRID_HEIGHT+SIN_GRID_Y_OFFSET+9);
      tft.print(String(time_division));
      delta_x=0;
      tft.drawLine(x+SIN_GRID_X_OFFSET, SIN_GRID_Y_OFFSET, x+SIN_GRID_X_OFFSET, SIN_GRID_HEIGHT+SIN_GRID_Y_OFFSET+3, TFT_WHITE); // The "3" value here serves a as "mark" for the corresponding text annotation. 
    } else {
      tft.drawLine(x+SIN_GRID_X_OFFSET, SIN_GRID_Y_OFFSET, x+SIN_GRID_X_OFFSET, SIN_GRID_HEIGHT+SIN_GRID_Y_OFFSET, TFT_WHITE);
    }
    x_old = x;
    time_division++;
  }

  // Draw the horizontal lines
  for (int y = SIN_GRID_HEIGHT; y >= 0; y -= gridStep_y) {
    tft.drawLine(SIN_GRID_X_OFFSET-3, y+SIN_GRID_Y_OFFSET, SIN_GRID_WIDTH + SIN_GRID_X_OFFSET+3, y+SIN_GRID_Y_OFFSET, TFT_WHITE);
    tft.setCursor(SIN_GRID_X_OFFSET-13, y+SIN_GRID_Y_OFFSET-3);
    tft.print(String(voltage_division));
    voltage_division++;
  }
  
  //new_sinewave_initialization();
  drawNewSignal_viz();

}

void drawNewSignalEditorScreen(){
  
  // Range value rules check
  
   if (sin_signalParams[selectedSignal][4] <= SIN_GRID_X_STEP_MIN){
    sin_signalParams[selectedSignal][4] = SIN_GRID_X_STEP_MIN;
  }
  if (sin_signalParams[selectedSignal][5] <= SIN_GRID_MIN_X_TRAVELLING_OFFSET){
     sin_signalParams[selectedSignal][5] = SIN_GRID_MIN_X_TRAVELLING_OFFSET;
  }
  if (sin_signalParams[selectedSignal][5] >= SIN_GRID_MAX_X_TRAVELLING_OFFSET){
     sin_signalParams[selectedSignal][5] = SIN_GRID_MAX_X_TRAVELLING_OFFSET;
  }

  
  tft.fillScreen(TFT_BLACK); // Clear the screen to black
  tft.setTextSize(2);             // set text size to 2
  tft.setCursor(0, 0);            // set cursor position
  tft.setTextColor(TFT_WHITE);
  tft.println("Sinus sur port " + String(selectedSignal+1));           // print "Menu" at the top of the screen
  tft.drawLine(tft.width(), 20, 0, 20, TFT_BLUE);
  tft.drawLine(tft.width(), 21, 0, 21, TFT_BLUE);
   // Draw the sin wave
  int y_pos = 35;
  int x_pos = 17;
  tft.setTextSize(1); 
  for (int i = 0; i < sin_numberOfParams+1; i++) {
    tft.setCursor(10 + (i%2)*120 + x_pos, y_pos + (i/2)*10); // Set cursor position
    if (i == sin_selectedParameter) {
      tft.setTextColor(TFT_BLUE);
      tft.print(sin_signalParamsNames[i]+" ");
      if(i != sin_numberOfParams) {
        tft.println(sin_signalParams[selectedSignal][i]);
      }
    } else {
      tft.setTextColor(TFT_WHITE);
      tft.print(sin_signalParamsNames[i]+" ");
          if(i != sin_numberOfParams){
            tft.println(sin_signalParams[selectedSignal][i]);
          }
    }
  }
  tft.setTextColor(TFT_WHITE);
  drawSinGrid();
}


void drawNewSignalEditorScreen_viz(){
  
  // Range value rules check
  
   if (viz_signalParams[0] <= SIN_GRID_X_STEP_MIN){
    viz_signalParams[0] = SIN_GRID_X_STEP_MIN;
  }
  if (viz_signalParams[1] <= SIN_GRID_MIN_X_TRAVELLING_OFFSET){
     viz_signalParams[1] = SIN_GRID_MIN_X_TRAVELLING_OFFSET;
  }
  if (viz_signalParams[1] >= SIN_GRID_MAX_X_TRAVELLING_OFFSET){
     viz_signalParams[1] = SIN_GRID_MAX_X_TRAVELLING_OFFSET;
  }
  if (viz_signalParams[2] >= SIGNAL_NUMBER){
     viz_signalParams[2] = SIGNAL_NUMBER;
  }
  if (viz_signalParams[2] < 0){
     viz_signalParams[2] = 0;
  }

  
  tft.fillScreen(TFT_BLACK); // Clear the screen to black
  tft.setTextSize(2);             // set text size to 2
  tft.setCursor(0, 0);            // set cursor position
  tft.setTextColor(TFT_WHITE);
  tft.println("Visualiser scene");
  //tft.println("Sinus sur port " + String(selectedSignal+1));           // print "Menu" at the top of the screen
  tft.drawLine(tft.width(), 20, 0, 20, TFT_BLUE);
  tft.drawLine(tft.width(), 21, 0, 21, TFT_BLUE);
   // Draw the sin wave
  int y_pos = 35;
  int x_pos = 17;
  tft.setTextSize(1); 
  for (int i = 0; i < viz_numberOfParams+1; i++) {
    tft.setCursor(10 + (i%2)*120 + x_pos, y_pos + (i/2)*10); // Set cursor position
    if (i == viz_selectedParameter) {
      tft.setTextColor(TFT_BLUE);
      tft.print(viz_signalParamsNames[i]+" ");
      if(i != viz_numberOfParams) {
        tft.println(viz_signalParams[i]);
      }
    } else {
      tft.setTextColor(TFT_WHITE);
      tft.print(viz_signalParamsNames[i]+" ");
          if(i != viz_numberOfParams){
            tft.println(viz_signalParams[i]);
          }
    }
  }
  tft.setTextColor(TFT_WHITE);
  drawSinGrid_viz();
}




////////////////////////////////////////////////////////////////////// CUSTOM SIGNAL /////////////////////////////////////////////////////////////////////


void adapt_PWM_frequency_value(){
  for (int i = 0 ; i < SIGNAL_NUMBER; i++){
     if (constant_signalParams[i][8] == MOTOR_DEVICE_SELECTION){
        PCA9685.setPWMFreq(PWM_PCA9685_OPERATING_FREQUENCY_MOTOR);
        Serial.println("The PWM frequency has been changed to 50Hz");
        break;
     } else {
        PCA9685.setPWMFreq(PWM_PCA9685_OPERATING_FREQUENCY);
        Serial.println("The PWM frequency has been changed to 1000Hz");
        //break;
     }
     Serial.println("Leaving for loop");
  }
}
void master_signal_buffering(){
  for(int i=0; i<SIGNAL_RESOLUTION; i++){
    master_signal_buffer[i] = master_signal[selectedSignal][i];
  }
}

void drawCustomSignal(){
  
  float number_of_fetched_points = ((float)SIN_GRID_WIDTH/constant_signalParams[selectedSignal][0])*(float)OPERATING_FREQUENCY;
  float pixel_distance = SIN_GRID_WIDTH/number_of_fetched_points;
  float index_value = constant_signalParams[selectedSignal][1] / pixel_distance;
  float new_signal_master_value = 0;
  float curseur_delta_from_barre1 = constant_signalParams[selectedSignal][4] - constant_signalParams[selectedSignal][2];
  float curseur_delta_from_barre2 = constant_signalParams[selectedSignal][3] - constant_signalParams[selectedSignal][4];
  float signal_duration = constant_signalParams[selectedSignal][7];
  
  for(float i=0; i<number_of_fetched_points; i++){
    if ( index_value < SIGNAL_RESOLUTION){
      int center_graph_offset = 0;// int((MAX_SYSTEM_VOLTAGE - (float)sin_signalParams[selectedSignal][0]*2)/MAX_SYSTEM_VOLTAGE * (float)SIN_GRID_HEIGHT)/2;
      if ( (int)index_value <= constant_signalParams[selectedSignal][2] || (int)index_value >= constant_signalParams[selectedSignal][3] ) {
        int normalized_to_screen = int( ( (DATATYPE_UINT16_RANGE-(float)master_signal[selectedSignal][(int)index_value]) / (float)DATATYPE_UINT16_RANGE ) * (float)SIN_GRID_HEIGHT );
        if (index_value < signal_duration){ // Points of the signal outside the duration time mark defined by the user will be set to white color if not included between "barre_1" and "barre_2".  
          tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance), SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen, TFT_RED);
          tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance), SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen+1, TFT_RED); // Double the pixel height of the line to make it more visible on the screen
          tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance)+1, SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen, TFT_RED); // Double the pixel width of the line to make it more visible on the screen
        } else {
          tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance), SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen, TFT_WHITE);
          tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance), SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen+1, TFT_WHITE); // Double the pixel height of the line to make it more visible on the screen
          tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance)+1, SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen, TFT_WHITE); // Double the pixel width of the line to make it more visible on the screen
        }
      } else if ( (int)index_value >= constant_signalParams[selectedSignal][2] && (int)index_value <= constant_signalParams[selectedSignal][3] ){
       
         if((int)index_value <= (int)constant_signalParams[selectedSignal][4]){ // Signal + (Signal * gain) * normalized_distance_to_cursor
            new_signal_master_value = master_signal_buffer[(int)index_value] + master_signal_buffer[(int)index_value]*(constant_signalParams[selectedSignal][5]-1)*pow(abs(index_value - constant_signalParams[selectedSignal][2])/ curseur_delta_from_barre1,constant_signalParams[selectedSignal][6]);
         } else {
            new_signal_master_value = master_signal_buffer[(int)index_value] + master_signal_buffer[(int)index_value]*(constant_signalParams[selectedSignal][5]-1)*pow(abs(index_value - constant_signalParams[selectedSignal][3])/ curseur_delta_from_barre2,constant_signalParams[selectedSignal][6]);
         }
         
         if(new_signal_master_value >= DATATYPE_UINT16_RANGE){ // Signal*gain range check
            master_signal[selectedSignal][(int)index_value] = DATATYPE_UINT16_RANGE;
         } else if (new_signal_master_value <= 0){
            master_signal[selectedSignal][(int)index_value] = 0;
         } else {
            master_signal[selectedSignal][(int)index_value] = (uint16_t) new_signal_master_value;
         }
        
        int normalized_to_screen = int( ( (DATATYPE_UINT16_RANGE-(float)master_signal[selectedSignal][(int)index_value]) / (float)DATATYPE_UINT16_RANGE ) * (float)SIN_GRID_HEIGHT );
        if(index_value<signal_duration){
          tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance), SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen, TFT_GREEN);
          tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance), SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen+1, TFT_GREEN); // Double the pixel height of the line to make it more visible on the screen
          tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance)+1, SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen, TFT_GREEN); // Double the pixel width of the line to make it more visible on the screen
        } else {
          tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance), SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen, TFT_WHITE);
          tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance), SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen+1, TFT_WHITE); // Double the pixel height of the line to make it more visible on the screen
          tft.drawPixel(SIN_GRID_X_OFFSET+(int)(i*pixel_distance)+1, SIN_GRID_Y_OFFSET+center_graph_offset+normalized_to_screen, TFT_WHITE); // Double the pixel width of the line to make it more visible on the screen
        }
         
      }
      if((int)index_value == (int)constant_signalParams[selectedSignal][2]){
        tft.drawLine(SIN_GRID_X_OFFSET + (i*pixel_distance),SIN_GRID_Y_OFFSET , SIN_GRID_X_OFFSET + (i*pixel_distance), SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT, TFT_YELLOW);
        tft.setTextColor(TFT_YELLOW);
        if(constant_signalParams[selectedSignal][2] <= constant_signalParams[selectedSignal][3]){
          tft.setCursor(SIN_GRID_X_OFFSET + (i*pixel_distance), SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT + 10);
          tft.println("A>");
        } else {
          tft.setCursor(SIN_GRID_X_OFFSET + (i*pixel_distance)-7, SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT + 10);
          tft.println("<A");
        }
        
      }
      if((int)index_value == (int)constant_signalParams[selectedSignal][3]){
        tft.drawLine(SIN_GRID_X_OFFSET + (i*pixel_distance),SIN_GRID_Y_OFFSET , SIN_GRID_X_OFFSET + (i*pixel_distance), SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT, TFT_YELLOW);
        tft.setTextColor(TFT_YELLOW);
        if(constant_signalParams[selectedSignal][3] <= constant_signalParams[selectedSignal][2]){
          tft.setCursor(SIN_GRID_X_OFFSET + (i*pixel_distance), SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT + 10);
          tft.println("B>");
        } else {
          tft.setCursor(SIN_GRID_X_OFFSET + (i*pixel_distance)-7, SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT + 10);
          tft.println("<B");
        }
      }
      if((int)index_value == (int)constant_signalParams[selectedSignal][4]){
        tft.drawLine(SIN_GRID_X_OFFSET + (i*pixel_distance),SIN_GRID_Y_OFFSET , SIN_GRID_X_OFFSET + (i*pixel_distance), SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT, TFT_ORANGE);
      }

      if((int)index_value == (int)constant_signalParams[selectedSignal][7]){
        tft.drawLine(SIN_GRID_X_OFFSET + (i*pixel_distance),SIN_GRID_Y_OFFSET , SIN_GRID_X_OFFSET + (i*pixel_distance), SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT, TFT_RED);
        tft.setTextColor(TFT_RED);
        tft.setCursor(SIN_GRID_X_OFFSET + (i*pixel_distance), SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT + 10);
        tft.println("T");
      }
      
    }
    index_value++;
    master_signal[selectedSignal][0] = master_signal[selectedSignal][1]; // Dirty bug fix: the first value of the array may be outside the reachable range when user is playing with "Barre_1" and the "gain" tool. 
  } 

  adapt_PWM_frequency_value();
 
}

void drawCustomGrid(){ 
  
  tft.setTextColor(TFT_WHITE);
  
  int gridStep_x = int(constant_signalParams[selectedSignal][0]);
  int gridStep_y = 20 ; // int(constant_signalParams[selectedSignal][6]);
  int gridStep_x_travelling_offset = int(constant_signalParams[selectedSignal][1]);
  int voltage_division=0;
  int time_division=0;
  int delta_x = SIN_GRID_X_TEXT_SPACING; // Init value to see the "0" marking
  int x_old = 0;

  tft.setTextSize(1);             // set text size to 2
  
  if (constant_device_selection == LED_DEVICE_SELECTION){
    tft.setCursor(SIN_GRID_X_OFFSET+3, SIN_GRID_Y_OFFSET + int(SIN_GRID_HEIGHT/2) - 25);
    tft.println(""); tft.println("V");  tft.println("O"); tft.println("L"); tft.println("T"); 
  }
  if (constant_device_selection == MOTOR_DEVICE_SELECTION){
    tft.setCursor(SIN_GRID_X_OFFSET+3, SIN_GRID_Y_OFFSET + int(SIN_GRID_HEIGHT/2) - 45);
    tft.println(""); tft.println("D");  tft.println("E"); tft.println("G"); tft.println("R");tft.println("E");tft.println("S");tft.println("");tft.println("x");tft.println("3");tft.println("6");
  }
  
  tft.setCursor(SIN_GRID_X_OFFSET + int(SIN_GRID_WIDTH/2) - 25, SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT + 27);
  tft.println("SECONDES");
  tft.setCursor(SIN_GRID_X_OFFSET + SIN_GRID_WIDTH-55, SIN_GRID_Y_OFFSET + SIN_GRID_HEIGHT + 27);
  tft.setTextColor(TFT_RED);
  tft.println("+-"+String(actual_button_step_increment_value));
  tft.setTextColor(TFT_WHITE);
  
  // Draw the vertical lines
  for (int x = -gridStep_x_travelling_offset; x <= SIN_GRID_WIDTH+gridStep_x_travelling_offset; x += gridStep_x) {
    if (x<=SIN_GRID_X_OFFSET-30){
      time_division++;
      continue;
    }
    if (x>=SIN_GRID_X_OFFSET+SIN_GRID_WIDTH-20){
      time_division++;
      continue;
    }
    delta_x = delta_x + (x-x_old); // Managing the spacing between the x grid annotations. 
    if (delta_x >= SIN_GRID_X_TEXT_SPACING){
      tft.setCursor(x+SIN_GRID_X_OFFSET-3, SIN_GRID_HEIGHT+SIN_GRID_Y_OFFSET+9);
      tft.print(String(time_division));
      delta_x=0;
      tft.drawLine(x+SIN_GRID_X_OFFSET, SIN_GRID_Y_OFFSET, x+SIN_GRID_X_OFFSET, SIN_GRID_HEIGHT+SIN_GRID_Y_OFFSET+3, TFT_WHITE); // The "3" value here serves a as "mark" for the corresponding text annotation. 
    } else {
      tft.drawLine(x+SIN_GRID_X_OFFSET, SIN_GRID_Y_OFFSET, x+SIN_GRID_X_OFFSET, SIN_GRID_HEIGHT+SIN_GRID_Y_OFFSET, TFT_WHITE);
    }
    x_old = x;
    time_division++;
  }

  // Draw the horizontal lines
  for (int y = SIN_GRID_HEIGHT; y >= 0; y -= gridStep_y) {
    tft.drawLine(SIN_GRID_X_OFFSET-3, y+SIN_GRID_Y_OFFSET, SIN_GRID_WIDTH + SIN_GRID_X_OFFSET+3, y+SIN_GRID_Y_OFFSET, TFT_WHITE);
    tft.setCursor(SIN_GRID_X_OFFSET-13, y+SIN_GRID_Y_OFFSET-3);
    tft.print(String(voltage_division));
    voltage_division++;
  }
  
  //new_sinewave_initialization();
  drawCustomSignal();

}


void drawConstantSignalEditorScreen(){
  
   if (constant_signalParams[selectedSignal][0] <= SIN_GRID_X_STEP_MIN){
    constant_signalParams[selectedSignal][0] = SIN_GRID_X_STEP_MIN;
  }
  if (constant_signalParams[selectedSignal][1] <= SIN_GRID_MIN_X_TRAVELLING_OFFSET){
     constant_signalParams[selectedSignal][1] = SIN_GRID_MIN_X_TRAVELLING_OFFSET;
  }
  if (constant_signalParams[selectedSignal][1] >= SIN_GRID_MAX_X_TRAVELLING_OFFSET){
     constant_signalParams[selectedSignal][1] = SIN_GRID_MAX_X_TRAVELLING_OFFSET;
  }
 // if (constant_signalParams[selectedSignal][2] >= constant_signalParams[selectedSignal][3]){ ////// Allows one to deactivate the influence of the "gain" when defining the modification range using barre1 and barre2
 //    constant_signalParams[selectedSignal][2] = constant_signalParams[selectedSignal][3]-1;
 // }
  if (constant_signalParams[selectedSignal][2] <= 0){
     constant_signalParams[selectedSignal][2] = 0;
  }
  if (constant_signalParams[selectedSignal][3] <= 0){
     constant_signalParams[selectedSignal][3] = 1;
  }
  if (constant_signalParams[selectedSignal][4] <= constant_signalParams[selectedSignal][2]){
     constant_signalParams[selectedSignal][4] = constant_signalParams[selectedSignal][2]+1;
  }
  if (constant_signalParams[selectedSignal][4] >= constant_signalParams[selectedSignal][3]){
    constant_signalParams[selectedSignal][4] = constant_signalParams[selectedSignal][3]-1;
  }

  

  // ADD MIN/MAX values for the "barres"

  tft.fillScreen(TFT_BLACK); // Clear the screen to black
  tft.setTextSize(2);             // set text size to 2
  tft.setCursor(0, 0);            // set cursor position
  tft.setTextColor(TFT_WHITE);
  tft.println("Sinus sur port " + String(selectedSignal+1)); // print "Menu" at the top of the screen
  tft.drawLine(tft.width(), 20, 0, 20, TFT_BLUE);
  tft.drawLine(tft.width(), 21, 0, 21, TFT_BLUE);
   // Draw the sin wave
  int y_pos = 35;
  int x_pos = 17;
  String text_name;
  String text_value;
  
  tft.setTextSize(1); 
  for (int i = 0; i < constant_numberOfParams+1; i++) {
    tft.setCursor(10 + (i%2)*120 + x_pos, y_pos + (i/2)*10); // Set cursor position
    text_name = (String)(constant_signalParamsNames[i]+" ");
    text_value = (String)(constant_signalParams[selectedSignal][i]);

    if(i == 8){ // HARDCODED DEVICE CHOICE DISPLAY
      if (constant_device_selection == LED_DEVICE_SELECTION){ 
        text_value = "LED";
      }
      if(constant_device_selection == MOTOR_DEVICE_SELECTION)
        text_value = "SERVO";
      }
    
    
    if (i == constant_selectedParameter) {
      tft.setTextColor(TFT_BLUE);
      tft.print(text_name);
      if(i != constant_numberOfParams) {
        tft.println(text_value);
      }
    } else {
      tft.setTextColor(TFT_WHITE);
      tft.print(text_name);
          if(i != constant_numberOfParams){
            tft.println(text_value);
          }
    }
  }
  tft.setTextColor(TFT_WHITE);
  
  drawCustomGrid();  
  
}

void setup() {
  Serial.begin(115200);
  Serial.printf("");

  tft.begin(); // Initialize the TFT screen
  tft.setRotation(0); // Set the rotation of the screen
  retrieve_data_from_flash(); // Loading signals from previous session maybe 
  tft.fillScreen(TFT_BLACK);
  //initialize_master_signals();
  Wire.begin();
  PCA9685.begin();
  PCA9685.setPWMFreq(PWM_PCA9685_OPERATING_FREQUENCY);

  constant_device_selection=LED_DEVICE_SELECTION;
  actual_button_step_increment_value = button_step_increment_values[button_step_index];
  selectedSignal = 0;
  selectedShape = 0;
  sin_selectedParameter=-1;
  currentMenuScreen = welcomeScreen;
  screenID = 0;
  drawMenuScreen();
  pinMode(button_navigation.PIN, INPUT_PULLUP);
  pinMode(button_selection.PIN, INPUT_PULLUP);
  pinMode(button_minus.PIN, INPUT_PULLUP);
  pinMode(button_plus.PIN, INPUT_PULLUP);
  attachInterrupt(button_navigation.PIN, isr_navigation_button, FALLING);
  attachInterrupt(button_selection.PIN, isr_selection_button, FALLING);
  attachInterrupt(button_minus.PIN, isr_minus_button, FALLING);
  attachInterrupt(button_plus.PIN, isr_plus_button, FALLING);
  
  
}

void check_user_input(){
  if(screenID == 0){ // Main menu (welcomeScreen)
        
          if (button_navigation.state) {
              if (currentMenuScreen.selection == currentMenuScreen.selectionMax-1){
                currentMenuScreen.selection=0;
              } else {
                currentMenuScreen.selection++;
              }
              drawMenuScreen();
            button_navigation.state = false;
          }
    
          if (button_selection.state) {
            if (currentMenuScreen.selection == 0){
              resetButtons();
              currentMenuScreen = signalSelectionScreen;
              screenID = 1;
              drawMenuScreen();
            } 
            
            
            if(currentMenuScreen.selection == 1){ //  Added for visualization
              resetButtons();
              screenID = 5;
              sin_selectedParameter = sin_numberOfParams;
              drawNewSignalEditorScreen_viz();
            }
            button_selection.state = false;
          }
          
      }

      if (screenID == 5){ // Signal Visualization
        
          if (button_navigation.state) {
            if (viz_selectedParameter == viz_numberOfParams){
                viz_selectedParameter=0;
              } else {
                viz_selectedParameter++;
              }
            drawNewSignalEditorScreen_viz();
            button_navigation.state = false;
          }
          
          if (button_selection.state) {
            if (viz_selectedParameter == viz_numberOfParams){ // Hit the "Retour" button 
              screenID = 0;
              resetButtons();
              currentMenuScreen = welcomeScreen;
              drawMenuScreen();
            } else {
              if(button_step_index < SIN_DISTINCT_INCREMENT_VALUES-1){
                button_step_index++;
              } else{
                button_step_index=0;
              }
              actual_button_step_increment_value = button_step_increment_values[button_step_index];
              drawNewSignalEditorScreen_viz();
            }
            button_selection.state = false;
          }
          
          if (button_minus.state) {
            if ((viz_selectedParameter != viz_numberOfParams) && (viz_selectedParameter!=-1)){
              viz_signalParams[viz_selectedParameter]-=actual_button_step_increment_value;
              drawNewSignalEditorScreen_viz();
            }
            button_minus.state = false;
          }
          
          if (button_plus.state) {
            if ((viz_selectedParameter != viz_numberOfParams) && (viz_selectedParameter!=-1)){
              viz_signalParams[viz_selectedParameter]+=actual_button_step_increment_value;
              drawNewSignalEditorScreen_viz();
            }
            button_plus.state = false;
          }
      }
    
    
      if (screenID == 1){ // Port selection menu (signalSelectionScreen)
        
          if (button_navigation.state) {
              if (currentMenuScreen.selection == currentMenuScreen.selectionMax-1){
                currentMenuScreen.selection=0;
              } else {
                currentMenuScreen.selection++;
              }
              drawMenuScreen();
            button_navigation.state = false;
          }
          
          if (button_selection.state) {
            if ((currentMenuScreen.selection != currentMenuScreen.selectionMax-1) && (currentMenuScreen.selection != -1 )){
              resetButtons();
              screenID = 2;
              selectedSignal=currentMenuScreen.selection;
              currentMenuScreen = formeSelectionScreen;
            }
            if (currentMenuScreen.selection == currentMenuScreen.selectionMax-1){
              screenID = 0;
              resetButtons();
              currentMenuScreen = welcomeScreen;
            }
            drawMenuScreen();
            button_selection.state = false;
          }
    
      }
    
      if (screenID == 2){ // Shape selection menu (formeSelectionScreen)
        
          if (button_navigation.state) {
              if (currentMenuScreen.selection == currentMenuScreen.selectionMax-1){
                currentMenuScreen.selection=0;
              } else {
                currentMenuScreen.selection++;
              }
              drawMenuScreen();
            button_navigation.state = false;
          }
          
          if (button_selection.state) {
            if ((currentMenuScreen.selection == 0)){
              resetButtons();
              screenID = 3;
              selectedShape=currentMenuScreen.selection+1;
              selectedShapes[selectedSignal] = selectedShape;
              sin_selectedParameter = sin_numberOfParams;
              drawNewSignalEditorScreen();
            }
            
            if ((currentMenuScreen.selection == 1)){
              resetButtons();
              screenID = 4;
              selectedShape=currentMenuScreen.selection+1;
              selectedShapes[selectedSignal] = selectedShape;
              master_signal_buffering(); // Required for live modification of the signal
              drawConstantSignalEditorScreen();
            }
            
            if (currentMenuScreen.selection == currentMenuScreen.selectionMax-1){
              screenID = 1;
              resetButtons();
              currentMenuScreen = signalSelectionScreen;
              drawMenuScreen();
            }
            button_selection.state = false;
          }
    
      }
      
    
    
      if (screenID == 3){ // Sin signal editor
        
          if (button_navigation.state) {
            if (sin_selectedParameter == sin_numberOfParams){
                sin_selectedParameter=0;
              } else {
                sin_selectedParameter++;
              }
            drawNewSignalEditorScreen();
            button_navigation.state = false;
          }
          
          if (button_selection.state) {
            if (sin_selectedParameter == sin_numberOfParams){ // Hit the "Retour" button 
              screenID = 2;
              write_data_to_flash(); 
              resetButtons();
              currentMenuScreen = formeSelectionScreen;
              drawMenuScreen();
            } else {
              if(button_step_index < SIN_DISTINCT_INCREMENT_VALUES-1){
                button_step_index++;
              } else{
                button_step_index=0;
              }
              actual_button_step_increment_value = button_step_increment_values[button_step_index];
              drawNewSignalEditorScreen();
            }
            button_selection.state = false;
          }
          
          if (button_minus.state) {
            if ((sin_selectedParameter != sin_numberOfParams) && (sin_selectedParameter!=-1)){
              sin_signalParams[selectedSignal][sin_selectedParameter]-=actual_button_step_increment_value;
              drawNewSignalEditorScreen();
            }
            button_minus.state = false;
          }
          
          if (button_plus.state) {
            if ((sin_selectedParameter != sin_numberOfParams) && (sin_selectedParameter!=-1)){
              sin_signalParams[selectedSignal][sin_selectedParameter]+=actual_button_step_increment_value;
              drawNewSignalEditorScreen();
            }
            button_plus.state = false;
          }
      }
    
      if (screenID == 4){ // Constant signal editor
        
        if (button_navigation.state) {
            if (constant_selectedParameter == constant_numberOfParams){
                constant_selectedParameter=0;
              } else {
                constant_selectedParameter++;
              }
            drawConstantSignalEditorScreen();
            button_navigation.state = false;
          }
          
          if (button_selection.state) {
            if (constant_selectedParameter == constant_numberOfParams){ // Correspond to "Retour"
              screenID = 2;
              write_data_to_flash();
              resetButtons();
              currentMenuScreen = formeSelectionScreen;
              drawMenuScreen();
            } else {
              if(button_step_index < SIN_DISTINCT_INCREMENT_VALUES-1){
                button_step_index++;
              } else{
                button_step_index=0;
              }
              actual_button_step_increment_value = button_step_increment_values[button_step_index];
              drawConstantSignalEditorScreen();
            }
            button_selection.state = false;
          }
          
          if (button_minus.state) {
            if ((constant_selectedParameter != constant_numberOfParams) && (constant_selectedParameter!=-1) /*HARDCODED CONDITION FOR DEVICE SELECTION -> */ && constant_selectedParameter!=8 ){
              constant_signalParams[selectedSignal][constant_selectedParameter]-=actual_button_step_increment_value;
              drawConstantSignalEditorScreen();
            } else {
              /*HARDCODED DEVICE SELECTION */
              constant_device_selection++;
              if (constant_device_selection%2 == LED_DEVICE_SELECTION){ 
                constant_device_selection = LED_DEVICE_SELECTION;
                constant_signalParams[selectedSignal][8] = LED_DEVICE_SELECTION;
              }
              if(constant_device_selection%2 == MOTOR_DEVICE_SELECTION){
                constant_device_selection = MOTOR_DEVICE_SELECTION;
                constant_signalParams[selectedSignal][8] = MOTOR_DEVICE_SELECTION;
              }
              drawConstantSignalEditorScreen();
           }
    
            button_minus.state = false;
          }
          
          if (button_plus.state) {
            if ((constant_selectedParameter != constant_numberOfParams) && (constant_selectedParameter!=-1) /*HARDCODED CONDITION FOR DEVICE SELECTION -> */ && constant_selectedParameter!=8){
              constant_signalParams[selectedSignal][constant_selectedParameter]+=actual_button_step_increment_value;
              drawConstantSignalEditorScreen();
            } else {
              /*HARDCODED DEVICE SELECTION */
              constant_device_selection++;
              if (constant_device_selection%2 == LED_DEVICE_SELECTION){ 
                constant_device_selection = LED_DEVICE_SELECTION;
                constant_signalParams[selectedSignal][8] = LED_DEVICE_SELECTION;
              }
              if(constant_device_selection%2 == MOTOR_DEVICE_SELECTION){
                constant_device_selection = MOTOR_DEVICE_SELECTION;
                constant_signalParams[selectedSignal][8] = MOTOR_DEVICE_SELECTION;
              }
              drawConstantSignalEditorScreen();
            }
    
            button_plus.state = false;
          }
      }
}



int main_signal_time_counter = 0;
int task_duration = (int)(1000/((float)OPERATING_FREQUENCY));
long int monitoring_clock_start;
long int monitoring_clock_stop;
long int remaining_slack_time;


void loop() {
  
    monitoring_clock_start = millis();
    
    if (main_signal_time_counter >= SIGNAL_RESOLUTION){
      main_signal_time_counter = 0;
    }
    check_user_input();

    for(int i = 0; i<SIGNAL_NUMBER; i++){
      if(constant_signalParams[i][8] == LED_DEVICE_SELECTION){
        PCA9685.setPWM(i,0,(int)(((float)master_signal[i][main_signal_time_counter%(int)constant_signalParams[i][7]]/((float)DATATYPE_UINT16_RANGE))*PWM_DUTY_RANGE));
      } else {
        PCA9685.setPWM(i,0,map((int)(((float)master_signal[i][main_signal_time_counter%(int)constant_signalParams[i][7]]/((float)DATATYPE_UINT16_RANGE))*PWM_DUTY_RANGE), 0, PWM_DUTY_RANGE, SERVO_DEFAULT_MIN_DUTY, SERVO_DEFAULT_MAX_DUTY));
        Serial.println(map((int)(((float)master_signal[i][main_signal_time_counter%(int)constant_signalParams[i][7]]/((float)DATATYPE_UINT16_RANGE))*PWM_DUTY_RANGE), 0, PWM_DUTY_RANGE, SERVO_DEFAULT_MIN_DUTY, SERVO_DEFAULT_MAX_DUTY));
      }
        
    }

    main_signal_time_counter++;
    
    monitoring_clock_stop = millis();
    remaining_slack_time = monitoring_clock_stop-monitoring_clock_start;
    
    if (remaining_slack_time<task_duration){
      delay(task_duration-remaining_slack_time);
      //Serial.print("Slack: "); Serial.print(task_duration-remaining_slack_time); Serial.println(" ms");
    }
    
    
  
}
