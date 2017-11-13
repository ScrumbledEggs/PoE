#include "Arduino.h"
#include <Servo.h>
#include <Adafruit_MotorShield.h>
#include <Adafruit_MS_PWMServoDriver.h>


Adafruit_MotorShield AFMS = Adafruit_MotorShield(); // Create the motor shield object with the default I2C address

Adafruit_StepperMotor *leftMotor = AFMS.getStepper(200, 2);
Adafruit_StepperMotor *rightMotor = AFMS.getStepper(200, 1);

Servo tool_servo;
const int servo_pin = 9; // Only 9 & 10 are supported

// Conversion from mm to stepper motor steps
const float steps_per_mm = 200.0 /* steps per rotation */ / 144.0 /* Circumference in mm */;

// Width between spools in mm
const int interspool_spacing = 970;

struct XY_Pos {
  int x;
  int y;
};

struct LR_Len {
  int l;
  int r;
};

LR_Len xy_to_lr(XY_Pos xy){
  LR_Len lr;
  lr.l = 1.0 * sqrt((xy.x*xy.x) + (xy.y*xy.y)) * steps_per_mm;
  lr.r = 1.0 * sqrt(((interspool_spacing - xy.x)*(interspool_spacing - xy.x)) + (xy.y*xy.y)) * steps_per_mm;
  return lr;
}


const int servo_marker = 90;
const int servo_off = 50;



// Maximum speed to run motors
const int max_speed = 50;


XY_Pos init_pos = { 540, 330 };


// Calculate init cable lengths
LR_Len cur_len = xy_to_lr(init_pos);


/* For Test Path */
// Units are in mm
const float x_scale = 5.0;
const float y_scale = 5.0;

// -10 for marker engage
// -20 for marker disengage

const int num_path = 29;
const float path[][2] =   {
  {0,0}, // Init @ origin
  {-10,0},
  {2,0}, // P
  {2,2},
  {0,2},
  {-20,0},
  {0,0},
  {-10,0},
  {0,4}, // end P
  {-20,0},
  {3,4}, // O
  {-10,0},
  {3,0},
  {6,0},
  {6,4},
  {3,4}, // end O
  {-20,0},
  {7,4}, // E
  {-10,0},
  {7,0},
  {9,0},
  {7,0},
  {7,2},
  {9,2},
  {7,2},
  {7,4},
  {9,4}, // end E
  {-20,0},
  {0,0}
};


// Because adafruit motorshield lib is dumb and FORWARD and BACKWARD aren't 1 and -1
int l_to_dir(float speed) {
  if(speed > 0)  return FORWARD;
  if(speed < 0)  return BACKWARD;
  return RELEASE;
}

int sign(float x) {
  if (x>0) return 1;
  if (x<0) return -1;
  return 0;
}

void run_motors(int dl_l, int dl_r){
  /* Given dl_l and dl_r, move the motors by that much
     dl_l (delta length left) in units step
     dl_r (delta length right) in units step */

  /* TODO: Rewrite to run both steppers at the same time, with their speeds modulated to make slope.
           This will probably require acceleration for ramping up and down */

  Serial.print("motors: ");
  Serial.print(dl_l);
  Serial.print(" ");
  Serial.println(dl_r);

  if(dl_l == 0 && dl_r == 0) return;

  // Init variables of remaining steps
  int l_steps = abs(dl_l);
  int r_steps = abs(dl_r);

  int l_dir = sign(dl_l);
  int r_dir = sign(dl_r);

  // init variables dl, dr which hold how many steps to take on each loop
  int dl, dr;

  while ((l_steps > 0) || (r_steps > 0)) {
    // Sets the smaller step to 1 and the larger step to ratio between r_steps and l_steps
    // Serial.print(l_steps);
    // Serial.print(" ");
    // Serial.print(r_steps);
    // Serial.print(" ");

    if (l_steps > r_steps){
      leftMotor->step(1, l_to_dir(dl_l), DOUBLE);
      l_steps -= 1;

      cur_len.l += 1 * l_dir;
      Serial.print(" 1 0 ");
    }
    else {
      rightMotor->step(1, l_to_dir(dl_r), DOUBLE);
      r_steps -= 1;

      cur_len.r += 1 * r_dir;
      Serial.print(" 0 1 ");
    }


    Serial.print(cur_len.l);
    Serial.print(" ");
    Serial.println(cur_len.r);
  }
}


void set_lengths(int len_l, int len_r){
  /* Moves the motors to set the string to the desired lengths in steps */

  Serial.print("len: ");
  Serial.print(len_l);
  Serial.print(" ");
  Serial.print(len_r);
  Serial.print(" ");

  int delta_l_l = len_l - cur_len.l;
  int delta_l_r = len_r - cur_len.r;

  run_motors(delta_l_l, delta_l_r);

  Serial.print("cur len: ");
  Serial.print(cur_len.l);
  Serial.print(" ");
  Serial.print(cur_len.r);
  Serial.println(" ");
}


void set_position(XY_Pos xy){
  /* Moves the marker to the position x,y
     x and y are in mm from top left corner */

  Serial.print(String(xy.x) + " " + String(xy.y) + " ");

  float new_length_l = 1.0 * sqrt( (xy.x*xy.x) + (xy.y*xy.y) ) * steps_per_mm;
  float new_length_r = 1.0 * sqrt( ((interspool_spacing - xy.x)*(interspool_spacing - xy.x)) + (xy.y*xy.y) ) * steps_per_mm;

  set_lengths(new_length_l, new_length_r);
}


void setup() {
  Serial.begin(9600);

  AFMS.begin();
  leftMotor->setSpeed(max_speed);
  rightMotor->setSpeed(max_speed);

  rightMotor->step(1, FORWARD, DOUBLE);
  leftMotor->step(1, FORWARD, DOUBLE);

  tool_servo.attach(servo_pin);
  tool_servo.write(48);

  Serial.println("begin.");

  Serial.print('Init lengths:');
  Serial.print(cur_len.l);
  Serial.print(' ');
  Serial.println(cur_len.r);


  set_position(XY_Pos {interspool_spacing/2, interspool_spacing/2});

  XY_Pos next_xy;

  // Run through the hard coded path
  for(int i = 0; i < num_path; i++){
    // Lots of messy stuff here.
    // The 50's are there to move the origin of the path to the center of the board
    if (path[i][0] == -10){
      tool_servo.write(servo_marker);
      Serial.println("Marker Down");
      delay(500);
    } else if (path[i][0] == -20){
      tool_servo.write(servo_off);
      Serial.println("Marker Up");
      delay(500);
    } else {
      next_xy.x = (path[i][0] * x_scale) + (interspool_spacing / 2);
      next_xy.y = (interspool_spacing / 2) + (path[i][1] * y_scale);

      set_position(next_xy);
    }
    // Let the motors rest
    delay(10);
  }
  //
  // leftMotor->release();
  // rightMotor->release();

  Serial.println("done!");
}


void loop() {
  // Spin the wheels
  delay(10);
}
