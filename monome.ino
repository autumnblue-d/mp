extern u8 monomeLedBuffer[256];
extern u8 monomeFrameDirty;

extern USBHost myusb;
extern USBSerial userial;

void serial_read() {
  // handle messages from monome device  
  myusb.Task();
  u8 buffer[64];
  u8 counter = 0;
  memset(buffer, 0, sizeof(buffer));
  while (userial.available()) {
    Serial.printf("serial_read: %d\n", counter);
    if (counter < sizeof(buffer)) {
      buffer[counter++] = userial.read();
    }
  }
  for(u8 i1=0; i1<counter; i1+=3) {
    static event_t e;
    e.type = kEventMonomeGridKey;
    u8* data = (u8*)(&(e.data));  
    data[0] = buffer[i1+1];
    data[1] = buffer[i1+2];
    data[2] = buffer[i1] & 1;
    event_post(&e);
    for(u8 i2=0; i2<3; i2++) {
      Serial.printf("%d: %.4x\n", i2, data[i2]);
    }
  }
}

void monome_setOverallInt(u8 i)
{
  // change overall led intensity
  monome_send2(0x17, (u8)i);
  delay(5);
}

void monome_setLed(u8 x, u8 y, u8 s)
{
  // set led state
  monome_send3(0x10 + s, x, y);
  delay(5);
}

void monome_setLedInt(u8 x, u8 y, u8 i) 
{
  // set individual led intensity
  monome_send4(0x18, x, y, i);
  delay(5);
}

void monome_setAll(u8 s)
{
  monome_send1(0x12 + s);
  delay(5);
}

void monome_setAllInt(u8 i)
{
  // set all leds to intensity i
  monome_send2(0x19, i);
  delay(5);
}

void monome_setRow(u8 x, u8 y, u8 d)
{
  monome_send4(0x15, x, y, d);
  delay(5);
}

void monome_setLevelRow(u8 x, u8 y, u8 d[8])
{
  monome_send3x8(0x1B, x, y, d);
  delay(5);
}

void monome_setLevelCol(u8 x, u8 y, u8 d[8])
{
  monome_send3x8(0x1C, x, y, d);
  delay(5);
}

void monome_setCol(u8 x, u8 y, u8 d)
{
  monome_send4(0x16, x, y, d); 
  delay(5);
}

void monome_setMap(u8 x, u8 y, u8 d[8])
{
  monome_send3x8(0x14, x, y, d);
  delay(5);
}

void monome_setLevelMap(u8 x, u8 y, u8 d[32])
{
  monome_send3x32(0x1A, x, y, d);
  delay(5);
}

void monome_send1(u8 b1)
{
  userial.write(b1);
  userial.flush();
}

void monome_send2(u8 b1, u8 b2)
{
  userial.write(b1);
  userial.write(b2);
  userial.flush(); 
}

void monome_send2x32(u8 b1, u8 b2, u8 b3[32])
{
  userial.write(b1);
  userial.write(b2);
  for (int i=0; i<32; i++) {
    userial.write(b3[i]);
  }
  userial.flush();
}

void monome_send3(u8 b1, u8 b2, u8 b3)
{
  userial.write(b1);
  userial.write(b2);
  userial.write(b3);
  userial.flush(); 
}

void monome_send3x8(u8 b1, u8 b2, u8 b3, u8 b4[8])
{
  userial.write(b1);
  userial.write(b2);
  userial.write(b3);
  for (int i=0; i<8; i++) {
    userial.write(b4[i]);
  }
  userial.flush();
}

void monome_send3x32(u8 b1, u8 b2, u8 b3, u8 b4[32])
{
  userial.write(b1);
  userial.write(b2);
  userial.write(b3);
  for (int i=0; i<32; i++) {
    userial.write(b4[i]);
  }
  userial.flush();
}

void monome_send4(u8 b1, u8 b2, u8 b3, u8 b4)
{
  userial.write(b1);
  userial.write(b2);
  userial.write(b3);
  userial.write(b4);
  userial.flush(); 
}

void monome_send5(u8 b1, u8 b2, u8 b3, u8 b4, u8 b5)
{
  userial.write(b1);
  userial.write(b2);
  userial.write(b3);
  userial.write(b4);
  userial.write(b5);
  userial.flush(); 
}

// set quadrant refresh flag from pos
void monome_calc_quadrant_flag(u8 x, u8 y) {
  if(x > 7) {
    if (y > 7) {
      monomeFrameDirty |= 0b1000;
    }
    else {
      monomeFrameDirty |= 0b0010;
    }
  } else {
    if (y > 7) {
      monomeFrameDirty |= 0b0100;
    }
    else {
      monomeFrameDirty |= 0b0001;
    }
  }
}

// set given quadrant dirty flag
void monome_set_quadrant_flag(u8 q) {
  monomeFrameDirty |= (1 << q);
}

void monome_mext_refresh() {
  u8 *ledBuffer = monomeLedBuffer;
  static u8 data[32];
  for(u8 i1=0; i1<8; i1++) {
    for(u8 i2=0; i2<4; i2++) {
      data[i2 + i1*4] = (ledBuffer[2*i2 + i1*16]<<4) | (ledBuffer[2*i2+1 + i1*16]) ;
    }
  }
  monome_setLevelMap(0, 0, data);
  for(u8 i1=0; i1<8; i1++) {
    for(u8 i2=0; i2<4; i2++) {
      data[i2 + i1*4] = (ledBuffer[2*i2+8 + i1*16]<<4) | (ledBuffer[2*i2+9 + i1*16]) ;
    }
  }
  // Serial.println("Refreshing Grid");
  monome_setLevelMap(8, 0, data);
}
