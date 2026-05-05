#define F_CPU 16000000UL // Tell the compiler our crystal is 16MHz

#include <avr/io.h>      // Standard AVR header for registers[cite: 6]
#include <util/delay.h>  // Predefined delay functions[cite: 6]

// --- LCD I2C Definitions (Bare Metal TWI) ---
#define LCD_ADDR 0x27 // Change to 0x3F if your screen is blank
#define LCD_BACKLIGHT 0x08
#define LCD_ENABLE 0x04
#define LCD_RW 0x02
#define LCD_RS 0x01

// --- Function Prototypes ---
void TWI_Init(void);
void TWI_Start(void);
void TWI_Stop(void);
void TWI_Write(uint8_t data);
void LCD_Send_Command(uint8_t cmd);
void LCD_Send_Data(uint8_t data);
void LCD_Init(void);
void LCD_Print(const char* str);
void LCD_Clear(void);
void Servo_Init(void);
void Trigger_Feeding(void);
int Get_Food_Distance(void);

// ==========================================
// Main Program Loop
// ==========================================
int main(void) {
    // 1. Initialize Inputs and Outputs[cite: 7]
    DDRD |= (1 << PD2);  // Ultrasonic TRIG (Pin 2) as Output
    DDRD &= ~(1 << PD3); // Ultrasonic ECHO (Pin 3) as Input
    DDRD &= ~(1 << PD4); // Button (Pin 4) as Input
    PORTD |= (1 << PD4); // Enable internal pull-up resistor for Button[cite: 7]

    // 2. Initialize Subsystems
    TWI_Init();      // Initialize I2C Hardware
    LCD_Init();      // Wake up the LCD
    Servo_Init();    // Set up Timer1 for PWM[cite: 4]

    LCD_Print("Smart Feeder");
    _delay_ms(2000);
    LCD_Clear();

    int distance;
    int percentage;
    const int containerDepth = 20; // 20cm max depth

    while (1) {
        // Read Distance
        distance = Get_Food_Distance();
        
        // Calculate Percentage (Simple Math)
        percentage = 100 - ((distance * 100) / containerDepth);
        if (percentage < 0) percentage = 0;
        if (percentage > 100) percentage = 100;

        // Display on LCD
        LCD_Send_Command(0x80); // Move cursor to top-left
        LCD_Print("Food Lvl: ");
        
        // Convert integer to characters manually (since no libraries)
        LCD_Send_Data((percentage / 100) + '0');
        LCD_Send_Data(((percentage / 10) % 10) + '0');
        LCD_Send_Data((percentage % 10) + '0');
        LCD_Print("%  ");

        // Check Button Press (Pin 4 goes LOW when pressed)[cite: 7]
        if (!(PIND & (1 << PD4))) {
            Trigger_Feeding();
            _delay_ms(1000); // Simple debounce[cite: 6]
        }

        _delay_ms(200); // Loop stability delay
    }
    return 0;
}

// ==========================================
// Subsystem Functions
// ==========================================

// --- Ultrasonic Sensor ---
int Get_Food_Distance(void) {
    long duration = 0;

    // Send 10us pulse to TRIG
    PORTD |= (1 << PD2);   // Set TRIG High[cite: 7]
    _delay_us(10);
    PORTD &= ~(1 << PD2);  // Set TRIG Low[cite: 7]

    // Wait for ECHO pin to go High
    while (!(PIND & (1 << PD3)));

    // Count how long ECHO stays High
    while (PIND & (1 << PD3)) {
        duration++;
        _delay_us(1); // 1 microsecond steps[cite: 6]
    }

    // Convert time to distance (cm)
    return (duration * 0.034) / 2;
}

// --- Servo Motor (Timer 1 Fast PWM) ---
void Servo_Init(void) {
    DDRB |= (1 << PB1); // Set Pin 9 (PB1) as Output for Servo[cite: 7]

    // Fast PWM Mode 14 (TOP = ICR1), Non-Inverting[cite: 4]
    TCCR1A = (1 << COM1A1) | (1 << WGM11);
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11); // Prescaler = 8[cite: 4]

    ICR1 = 39999; // 50Hz frequency (20ms period)
    OCR1A = 2000; // Initial Position (0 degrees)
}

void Trigger_Feeding(void) {
    LCD_Send_Command(0xC0); // Move cursor to line 2
    LCD_Print("Dispensing...   ");
    
    OCR1A = 4000; // Open dispenser (~90 degrees)
    _delay_ms(1000);
    OCR1A = 2000; // Close dispenser (0 degrees)
    
    LCD_Clear();
}

// --- Bare Metal I2C (TWI) for LCD ---
void TWI_Init(void) {
    TWSR = 0x00; // Prescaler = 1
    TWBR = 0x48; // SCL frequency ~ 100kHz
    TWCR = (1 << TWEN); // Enable TWI
}

void TWI_Start(void) {
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT))); // Wait for start condition to transmit
}

void TWI_Stop(void) {
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
}

void TWI_Write(uint8_t data) {
    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT))); // Wait for data to transmit
}

void LCD_Write_I2C(uint8_t data) {
    TWI_Start();
    TWI_Write(LCD_ADDR << 1); // Send Address with Write bit
    TWI_Write(data | LCD_BACKLIGHT); // Send Data + Backlight state
    TWI_Stop();
}

void LCD_Pulse_Enable(uint8_t data) {
    LCD_Write_I2C(data | LCD_ENABLE); // Enable HIGH
    _delay_us(1);
    LCD_Write_I2C(data & ~LCD_ENABLE); // Enable LOW
    _delay_us(50);
}

void LCD_Send_4Bit(uint8_t data) {
    LCD_Write_I2C(data);
    LCD_Pulse_Enable(data);
}

void LCD_Send_Command(uint8_t cmd) {
    uint8_t high_nibble = cmd & 0xF0;
    uint8_t low_nibble = (cmd << 4) & 0xF0;
    LCD_Send_4Bit(high_nibble); // RS = 0 for Command
    LCD_Send_4Bit(low_nibble);
}

void LCD_Send_Data(uint8_t data) {
    uint8_t high_nibble = (data & 0xF0) | LCD_RS;
    uint8_t low_nibble = ((data << 4) & 0xF0) | LCD_RS;
    LCD_Send_4Bit(high_nibble); // RS = 1 for Data
    LCD_Send_4Bit(low_nibble);
}

void LCD_Init(void) {
    _delay_ms(50); // Wait for LCD to power up
    LCD_Send_4Bit(0x30); _delay_ms(5);
    LCD_Send_4Bit(0x30); _delay_us(150);
    LCD_Send_4Bit(0x30);
    LCD_Send_4Bit(0x20); // Switch to 4-bit mode
    
    LCD_Send_Command(0x28); // 2 lines, 5x8 matrix
    LCD_Send_Command(0x0C); // Display ON, Cursor OFF
    LCD_Send_Command(0x06); // Increment cursor
    LCD_Clear();
}

void LCD_Clear(void) {
    LCD_Send_Command(0x01); // Clear command
    _delay_ms(2);
}

void LCD_Print(const char* str) {
    while (*str) {
        LCD_Send_Data(*str++);
    }
}