
#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>

// LCD I2C Configuration (Standard PCF8574 backpack)
#define LCD_ADDR       0x27
#define LCD_BACKLIGHT  0x08
#define LCD_ENABLE     0x04
#define LCD_RW         0x02
#define LCD_RS         0x01

// Global clock for scheduling
volatile uint32_t ms_ticks = 0;            
const uint32_t AUTO_FEEDING_INTERVAL = 60000; // Trigger every 1 minutes

// Function Prototypes
void TWI_Init(void); void TWI_Start(void); void TWI_Stop(void);
void TWI_Write(uint8_t data); void LCD_Send_Command(uint8_t cmd);
void LCD_Send_Data(uint8_t data); void LCD_Init(void);
void LCD_Print(const char* str); void LCD_Clear(void);
void Servo_Init(void); void Trigger_Feeding(void);
float Get_Food_Distance(void); void Timer0_Init(void);

// Standard 1ms timer tick
ISR(TIMER0_COMPA_vect) { 
    ms_ticks++; 
}

int main(void) {
    // Setup IO: D2=Trig(Out), D3=Echo(In), D4=Button(In)
    DDRD |= (1 << PD2); 
    DDRD &= ~(1 << PD3); 
    DDRD &= ~(1 << PD4);
    PORTD |= (1 << PD4); // Activate pull-up for the manual button

    // Initializing hardware blocks
    TWI_Init(); 
    LCD_Init(); 
    Servo_Init(); 
    Timer0_Init(); 

    LCD_Print("Smart Feeder");
    _delay_ms(2000);
    LCD_Clear();

    float distance;
    int percentage;
    
    // --- CALIBRATION (Finetuned for 3cm container) ---
    const float containerDepth = 3.0; // Point where we say it's 0%
    const float minDistance = 0.6;    // Point where we say it's 100%
    
    uint32_t last_feed_time = 0;

    while (1) {
        // Run 10 samples to smooth out the data (Aggressive Averaging)
        float sum = 0;
        for(int i = 0; i < 10; i++) {
            sum += Get_Food_Distance();
            _delay_ms(10);
        }
        distance = sum / 10.0;

        // Accurate Percentage Math using floating point for high resolution
        float range = containerDepth - minDistance;
        if (distance <= minDistance) {
            percentage = 100;
        } else if (distance >= containerDepth) {
            percentage = 0;
        } else {
            // Calculate actual percentage based on calibrated range
            float calcPerc = ((containerDepth - distance) / range) * 100.0;
            percentage = (int)(calcPerc + 0.5); // Rounding
        }
        
        // Safety bounds
        if (percentage > 100) percentage = 100;
        if (percentage < 0) percentage = 0;

        // --- Update LCD Display ---
        LCD_Send_Command(0x80);
        LCD_Print("Food Lvl: ");
        LCD_Send_Command(0x8A); // Jump to char 11 to keep number right-aligned

        if (percentage == 100) {
            LCD_Print("100%");
        } else {
            // Adding dynamic spacing to prevent text shifting
            if (percentage < 10) LCD_Print("  "); 
            else LCD_Print(" ");            
            
            LCD_Send_Data((percentage / 10) + '0');
            LCD_Send_Data((percentage % 10) + '0');
            LCD_Print("% "); 
        }

        // Check for manual button press
        if (!(PIND & (1 << PD4))) {
            Trigger_Feeding();
            last_feed_time = ms_ticks; // Update timer after manual feed
            _delay_ms(1000); 
        }

        // Automatic feed check
        if (ms_ticks - last_feed_time >= AUTO_FEEDING_INTERVAL) {
            Trigger_Feeding();
            last_feed_time = ms_ticks; 
        }

        _delay_ms(200); // Wait for sensor to settle
    }
}

// Precise Ultrasonic Read using Software Timing
float Get_Food_Distance(void) {
    uint32_t count = 0;
    const uint32_t timeout = 400000;
    
    // Triggering the sensor
    PORTD |= (1 << PD2);
    _delay_us(10);
    PORTD &= ~(1 << PD2);

    // Wait for the Echo start
    while (!(PIND & (1 << PD3)) && count < timeout) count++;
    
    count = 0;
    // Capture high-pulse duration (The tight loop)
    while ((PIND & (1 << PD3)) && count < timeout) { 
        count++; 
    }
    
    // High-accuracy multiplier calibrated for 16MHz clock cycles
    return (float)(count * 0.00536);
}

// 1ms System Heartbeat
void Timer0_Init(void) {
    TCCR0A = (1 << WGM01); // CTC mode
    TCCR0B = (1 << CS01) | (1 << CS00); // Prescaler 64
    OCR0A = 249; // (16MHz/64)/1000 = 250 ticks
    TIMSK0 = (1 << OCIE0A); 
    sei(); 
}

// Config for Pin 9 (PB1) Servo control
void Servo_Init(void) {
    DDRB |= (1 << PB1); 
    TCCR1A = (1 << COM1A1) | (1 << WGM11);
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11); // Prescaler 8
    ICR1 = 39999; // 50Hz frequency
    OCR1A = 2000; // Closed pos
}

// Food dispensing routine
void Trigger_Feeding(void) {
    LCD_Send_Command(0xC0); // Move to Row 2
    LCD_Print("FEEDING...      ");
    
    OCR1A = 4000; // Open dispenser
    _delay_ms(3000);
    OCR1A = 2000; // Close dispenser
    
    LCD_Send_Command(0xC0);
    LCD_Print("                "); // Clear text
}

// --- I2C / TWI Driver Logic ---

void TWI_Init(void) { 
    TWSR = 0x00; 
    TWBR = 0x48; // Set to 100kHz
    TWCR = (1 << TWEN); 
}

void TWI_Start(void) { 
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN); 
    while (!(TWCR & (1 << TWINT))); 
}

void TWI_Stop(void) { 
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN); 
}

void TWI_Write(uint8_t data) { 
    TWDR = data; 
    TWCR = (1 << TWINT) | (1 << TWEN); 
    while (!(TWCR & (1 << TWINT))); 
}

// Low level LCD communication over I2C
void LCD_Write_I2C(uint8_t data) { 
    TWI_Start(); 
    TWI_Write(LCD_ADDR << 1); 
    TWI_Write(data | LCD_BACKLIGHT); 
    TWI_Stop(); 
}

void LCD_Pulse_Enable(uint8_t data) { 
    LCD_Write_I2C(data | LCD_ENABLE); 
    _delay_us(1); 
    LCD_Write_I2C(data & ~LCD_ENABLE); 
    _delay_us(50); 
}

void LCD_Send_4Bit(uint8_t data) { 
    LCD_Write_I2C(data); 
    LCD_Pulse_Enable(data); 
}

void LCD_Send_Command(uint8_t cmd) { 
    LCD_Send_4Bit(cmd & 0xF0); 
    LCD_Send_4Bit((cmd << 4) & 0xF0); 
}

void LCD_Send_Data(uint8_t data) { 
    LCD_Send_4Bit((data & 0xF0) | LCD_RS); 
    LCD_Send_4Bit(((data << 4) & 0xF0) | LCD_RS); 
}

void LCD_Init(void) { 
    _delay_ms(50); 
    LCD_Send_4Bit(0x30); _delay_ms(5);
    LCD_Send_4Bit(0x30); _delay_us(150);
    LCD_Send_4Bit(0x30);
    LCD_Send_4Bit(0x20); // 4-bit mode activation
    LCD_Send_Command(0x28); // 2 Lines, 5x8 font
    LCD_Send_Command(0x0C); // Display ON
    LCD_Send_Command(0x06); // Auto-increment cursor
    LCD_Clear(); 
}

void LCD_Clear(void) { 
    LCD_Send_Command(0x01); 
    _delay_ms(2); 
}

void LCD_Print(const char* str) { 
    while (*str) LCD_Send_Data(*str++); 
}