#include <Encoder.h>
#include <Wire.h>


const int NUM_KNOBS = 5;
Encoder* knobs[NUM_KNOBS];

int knob_pins[NUM_KNOBS][2] = { {10,11},
                                {8,9},
                                {6,7},
                                {4,5},
                                {2,3}};
long positions[NUM_KNOBS];
int substeps = 0;
unsigned long start_time = 0;

//button state
/*const int buttonPin = 8;     // the number of the pushbutton pin
int buttonState = 0;
int is_down = 0;*/



void setup() {//message stucture, CVxxxx00 down, CVxxxx01 up, CVxxxx10 cw, CVxxxx11 ccw

  Wire.begin(4);
  Wire.onReceive(receiveEvent);
  //buttons
  for(int i = 0; i < NUM_KNOBS; i++)
    knobs[i] = new Encoder( knob_pins[i][0] , knob_pins[i][1] );
  Serial.begin(9600 , SERIAL_8N1);
}

void send_message(int index, int value ,char* message){
  
  Serial.print(index);
  Serial.print(value);
  Serial.println(message);
}

void process_knob(int index){
  long new_value;
  new_value = knobs[index]->read();
  if (new_value != positions[index]) {
    if(substeps==0)
      start_time = millis();
    
    substeps++;
    if(substeps == 4){
      unsigned long duration = millis() - start_time;
      duration =  min( duration , 400 );
      Serial.print("CV");
      Serial.print(index);

      if(duration < 100){
        Serial.print("0");
        if(duration < 10){
          Serial.print("0");
        }
      }
      Serial.print(duration);
      if(new_value > positions[index]){
        Serial.println( "11" );
      }else if(new_value < positions[index]){
        Serial.println( "10" );
      }
    }
    
    if(substeps == 4){
      substeps = 0;
    }
    positions[index] = new_value;
  }
}

void loop() {

  for(int i = 0; i < NUM_KNOBS; i++)
    process_knob(i);
  
  /*buttonState = digitalRead(buttonPin);
  if (buttonState == HIGH && is_down == false) {
      send_message( 0 , "xxx00" );
      is_down = true;
  } else if( buttonState == LOW && is_down == true){
      send_message( 0 , "xxx01" );
      is_down = false;
  }*/
  
  // if a character is sent from the serial monitor,
  // reset both back to zero.
  /*if (Serial.available()) {
    Serial.read();
    Serial.println("Reset both knobs to zero");
    knobLeft.write(0);
  }*/
}

void receiveEvent(int num){
  while(1 < Wire.available()) // loop through all but the last
  {
    char c = Wire.read(); // receive byte as a character
    //Serial.print(c);         // print the character
  }
  int i = Wire.read();
  Serial.print("CV");
  Serial.print(i);         // print the integer*/
  Serial.println("00001");
}
