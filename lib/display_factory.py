import machine

_SPI_TYPES = ('ili9341', 'ili9342', 'ili9488')


def make_display(cfg):
    """
    Return the right display driver instance for the given device config dict.
    Both main.py (splash) and modules/display.py call this so the init logic
    lives in one place.
    Returns None if the display type is unknown.
    """
    display_type = cfg.get('display', 'ssd1306')
    pins = cfg.get('display_pins', {})
    res  = cfg.get('display_res', [128, 64])

    if display_type == 'ssd1306':
        from ssd1306 import SSD1306_I2C
        i2c = machine.SoftI2C(
            scl=machine.Pin(pins.get('scl', 18)),
            sda=machine.Pin(pins.get('sda', 17)),
        )
        return SSD1306_I2C(res[0], res[1], i2c)

    if display_type in _SPI_TYPES:
        from ili9341 import ILI9341
        spi = machine.SPI(
            1,
            baudrate=40_000_000,
            polarity=0, phase=0,
            sck=machine.Pin(pins['clk']),
            mosi=machine.Pin(pins['mosi']),
        )
        cs  = machine.Pin(pins['cs'], machine.Pin.OUT, value=1)
        dc  = machine.Pin(pins['dc'], machine.Pin.OUT)
        rst = machine.Pin(pins['rst'], machine.Pin.OUT) if 'rst' in pins else None
        bl  = machine.Pin(pins['bl'],  machine.Pin.OUT) if 'bl'  in pins else None
        return ILI9341(spi, cs, dc, rst, bl, res[0], res[1])

    return None
