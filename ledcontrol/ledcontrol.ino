/**
 * Single button LED controller to replace a defective standard controller for
 * non-addressable LEDs. Regular mode allows to adjust HSV color space
 * parameters. A TCS34725 color sensor detects ambient color that is applied
 * to the LEDs. In ambient mode the button controls only brightness.
 */

bool ambientColorEnabled = true; // start in ambient color mode

int hue = 256;        // [0,359], adjusted color mode
int sat = 66;         // [0,100], adjusted color mode
int brightness = 152; // [0,255], globally for both modes

 // duration of sweep through the full value range when adjusting color
double sweepSeconds = 9.;

// sensor threshold: if sum of sensor colors or clear value is below,
// measurement is considered noise; depends on sensor integration time and
// gain the sensor is initialized with.
int thresh = 40;

// enable LED gamma correction (from Adafruit example)
bool useGamma = false;

// pins
const int pinButton = 8;
const int pinRed = 9;
const int pinGreen = 10;
const int pinBlue = 11;

// color correct sensor readings from average values sensing white
double avgR = 0.24; // same value for all three of them disables correction
double avgG = 0.37;
double avgB = 0.39;

// if true, the average sensor values are determined over the first n samples
// average values of a white image (screen) should be printed and set above
bool colorBalance = false;
const int nSamples = 60;

// TCS34725 ///////////////////////////////////////////////////////////////////

#include "Adafruit_TCS34725.h"

uint16_t red, green, blue, clear;
double ambientLuminance = brightness / 255.;
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_16X);

// don't change any values of globals below ///////////////////////////////////

struct ColorRGB
{
    byte r;
    byte g;
    byte b;
};

void fadeToRGB(ColorRGB c, int steps = 100, int fadeDelay = 10);

byte gamma[256];

double corR; // color correction will be calculated from avg color values given above
double corG;
double corB;

// globals ////////////////////////////////////////////////////////////////////

int adj = 0; // index for switching adjustment paramter
bool lastButtonState = LOW;
long int pushTime = 0;

ColorRGB currentColor = {};  // interpolation color
ColorRGB adjustedColor = {}; // color set by button interaction
ColorRGB ambientColor = {};  // color updated by color sensor

// moving mean /////////////////////////////////////////////////////////////////

const int windowSize = 8;

struct MovingMean
{
    float mmValues[windowSize] = {};
    float mmSum = 0.;
    int mmIndex = 0;
    int mmSize = 0;

    float mean = 0.;

    float addValue(float val)
    {
        mmSum = mmSum - mmValues[mmIndex] + val;
        mmValues[mmIndex] = val;
        mmIndex = (mmIndex + 1) % windowSize;
        mmSize = min(mmSize + 1, windowSize);
        mean = mmSum / mmSize;
        return mean;
    }
};

MovingMean ambientRed; // color values filtered before adding to mean
MovingMean ambientGreen;
MovingMean ambientBlue;
MovingMean luminance; // value continiously added if detected by sensor (clear > 0)

// auxiliary //////////////////////////////////////////////////////////////////

template <typename T>
const T clamp(T value, T a, T b)
{
    return value < a ? a : (value > b ? b : value);
}

template <class T, class U>
const T interp(const T &a, const T &b, const U &t)
{
    if (t < (U)1e-3)
    {
        return a;
    }
    if (t > (U)(1. - 1e-3))
    {
        return b;
    }
    return (T)(a + t * (b - a));
}

// main ///////////////////////////////////////////////////////////////////////

void setup()
{
    //Serial.begin(115200);

    pinMode(pinRed, OUTPUT);
    pinMode(pinGreen, OUTPUT);
    pinMode(pinBlue, OUTPUT);
    pinMode(pinButton, INPUT_PULLUP);

    applyColorRGB({0, 0, 0});

    for (int i = 0; i < 256; i++)
    {
        gamma[i] = pow(i / 255., 2.5) * 255;
    }

    correctColor(avgR, avgG, avgB);

    if (!tcs.begin())
    {
        for (;;)
        {
        }
    }

    if (!ambientColorEnabled)
    {
        tcs.disable(); // sleep
    }

    // initialize
    double h = hue / 360.;
    double s = sat / 100.;
    double v = brightness / 255.;
    initColors(h, s, v);
    sweepToHSV(1. + h, s, v);
}

void initColors(double h, double s, double v)
{
    adjustedColor = hsv1_to_rgb255(h, s, v);
    for (int i = 0; i < windowSize; i++)
    {
        ambientRed.addValue(adjustedColor.r / 255.);
        ambientGreen.addValue(adjustedColor.g / 255.);
        ambientBlue.addValue(adjustedColor.b / 255.);
        luminance.addValue(brightness / 255.);
    }
    ambientColor = adjustedColor;
}

void loop()
{
    buttonLoop();
    ambientLoop();
}

void buttonLoop()
{
    bool buttonState = digitalRead(pinButton);

    if (lastButtonState == HIGH && buttonState == LOW) // got pushed
    {
        pushTime = millis();
    }
    else if (lastButtonState == LOW && buttonState == HIGH) // got released
    {
        if (millis() - pushTime < 250) // released after short push
        {
            // increment index to change next hsv value
            if (!ambientColorEnabled)
            {
                adj = (adj + 1) % 3;

                // provide user feedback
                ColorRGB inv = {255 - adjustedColor.r, 255 - adjustedColor.g, 255 - adjustedColor.b};
                for (int i = 0; i < adj + 1; i++)
                {
                    fadeToRGB(inv, 10, 8);
                    fadeToRGB(adjustedColor, 10, 5);
                }
            }
        }
        else if (millis() - pushTime < 1000) // released after long push
        {
            // toggle ambient/fixed color modes
            ambientColorEnabled = !ambientColorEnabled;

            if (ambientColorEnabled)
            {
                tcs.enable();                 // enable sensor
                ambientColor = adjustedColor; // force fade
            }
            else
            {
                tcs.disable(); // go to sleep
                fadeToRGB(adjustedColor);
                adj = 0;
            }
        }
    }
    else if (buttonState == LOW && millis() - pushTime > 1000) // press and hold
    {
        int range = 100;

        // adjust values by sweeping through their range
        if (ambientColorEnabled || adj == 0)
        {
            range = 255;
            brightness -= 1;
            if (brightness < 0)
            {
                brightness = 255;
            }
        }
        else if (adj == 1) // change hue
        {
            range = 359;
            hue += 1;
            if (hue > 359)
            {
                hue = 0;
            }
        }
        else if (adj == 2) // change saturation
        {
            range = 100;
            sat -= 1;
            if (sat < 0)
            {
                sat = 100;
            }
        }

        if (!ambientColorEnabled) // apply only in color mode; ambient mode is updated in loop
        {
            adjustedColor = hsv1_to_rgb255(hue / 359., sat / 100., brightness / 255.);
            applyColorRGB(adjustedColor);
            delay(1000. / (range / sweepSeconds));
        }
    }

    lastButtonState = buttonState;
}

void ambientLoop()
{
    if (colorBalance)
    {
        double r, g, b;
        collectAverageColor(r, g, b);
        correctColor(r, g, b);
        colorBalance = false;
    }

    if (ambientColorEnabled)
    {
        tcs.getRawData(&red, &green, &blue, &clear);

        double r, g, b;

        if (clear > 0)
        {
            double c = clear;
            r = red / c;
            g = green / c;
            b = blue / c;

            if ((red ^ green ? blue : red) && (red + green + blue) > thresh)
            {
                r *= corR;
                g *= corG;
                b *= corB;
                r = clamp(r, 0., 1.);
                g = clamp(g, 0., 1.);
                b = clamp(b, 0., 1.);

                luminance.addValue(0.2126 * r + 0.7152 * g + 0.0722 * b);
                ambientLuminance = luminance.mean;

                r = ambientRed.addValue(r);
                g = ambientGreen.addValue(g);
                b = ambientBlue.addValue(b);
            }
        }

        if (clear < thresh)
        {
            // slowly interpolate from ambient luminance (set with last sensor detection) to user-set max brightness
            ambientLuminance = interp(ambientLuminance, brightness / 255., 0.006);
            ambientRed.addValue(ambientLuminance);
            ambientGreen.addValue(ambientLuminance);
            ambientBlue.addValue(ambientLuminance);
        }

        ColorRGB target = {ambientRed.mean * brightness, ambientGreen.mean * brightness, ambientBlue.mean * brightness};
        ambientColor = interpColorRGB(ambientColor, target, 0.66);
        applyColorRGB(ambientColor);
    }
}

void collectAverageColor(double &r, double &g, double &b)
{
    r = 0.;
    g = 0.;
    b = 0.;

    for (int i = 0; i < nSamples; i++)
    {
        tcs.getRawData(&red, &green, &blue, &clear);

        if (clear > 0)
        {
            double c = clear;
            r += red / c;
            g += green / c;
            b += blue / c;
        }
    }

    r /= (double)nSamples;
    g /= (double)nSamples;
    b /= (double)nSamples;

    /*Serial.print(r);
    Serial.print("\t");
    Serial.print(g);
    Serial.print("\t");
    Serial.print(b);
    Serial.print("\t");*/
}

void correctColor(double r, double g, double b)
{
    double sum = (r + g + b);
    corR = sum / (3. * r);
    corG = sum / (3. * g);
    corB = sum / (3. * b);
}

void sweepToHSV(double h1, double s1, double v1)
{
    double h, s, v, t;
    for (int i = 0; i <= h1 * 360; i++)
    {
        t = i / (h1 * 360.);
        h = (i % 360) / 360.; // hue in range [0,1)
        s = interp(1., s1, t);
        v = interp(1., v1, t);
        applyColorRGB(hsv1_to_rgb255(h, s, v));
        delay(interp(4, 32, t * t));
    }
}

// RGB color space functions //////////////////////////////////////////////////

void setColorRGB(ColorRGB c)
{
    if (useGamma)
    {
        analogWrite(pinRed, gamma[(int)c.r]);
        analogWrite(pinGreen, gamma[(int)c.g]);
        analogWrite(pinBlue, gamma[(int)c.b]);
    }
    else
    {
        analogWrite(pinRed, c.r);
        analogWrite(pinGreen, c.g);
        analogWrite(pinBlue, c.b);
    }
}

void applyColorRGB(ColorRGB c)
{
    currentColor = c;
    setColorRGB(c);
}

void fadeToRGB(ColorRGB c, int steps = 100, int fadeDelay = 10)
{
    float t = 0.0f;
    for (int i = 0; i < steps; i++)
    {
        t = (i + 1) / (float)steps;
        applyColorRGB(interpColorRGB(currentColor, c, t));
        delay(fadeDelay);
    }
}

ColorRGB interpColorRGB(const ColorRGB &c1, const ColorRGB &c2, float t)
{
    byte r = interp(c1.r, c2.r, t);
    byte g = interp(c1.g, c2.g, t);
    byte b = interp(c1.b, c2.b, t);
    return {r, g, b};
}

// HSV color space functions //////////////////////////////////////////////////

static void hsv2rgb(double h, double s, double v, double &r, double &g, double &b)
{
    int i = (int)(h * 6);
    double f = h * 6 - i;
    double p = v * (1 - s);
    double q = v * (1 - s * f);
    double t = v * (1 - s * (1 - f));

    switch (i % 6)
    {
    case 0:
        r = v, g = t, b = p;
        break;
    case 1:
        r = q, g = v, b = p;
        break;
    case 2:
        r = p, g = v, b = t;
        break;
    case 3:
        r = p, g = q, b = v;
        break;
    case 4:
        r = t, g = p, b = v;
        break;
    case 5:
        r = v, g = p, b = q;
        break;
    }

    r = r < 0. ? 0. : (r > 1. ? 1. : r);
    g = g < 0. ? 0. : (g > 1. ? 1. : g);
    b = b < 0. ? 0. : (b > 1. ? 1. : b);
}

// mixed color space functions ////////////////////////////////////////////////

static ColorRGB hsv1_to_rgb255(double h, double s, double v)
{
    double r, g, b;
    hsv2rgb(h, s, v, r, g, b);
    return {r * 255, g * 255, b * 255};
}