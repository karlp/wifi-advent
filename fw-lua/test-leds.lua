-- Cycle the leds across the entire tree.
-- basic test that the hardware works.
ws2812.init()
buf = ws2812.newBuffer(7, 3)
buf:set(1, 0,20,0)
buf:set(2, 20,00,0)
buf:set(3, 00,00,20)
buf:set(4, 40,10,20)

tmr.alarm(0, 200, 1, function()
	ws2812.write(buf)
	buf:shift(1, ws2812.SHIFT_CIRCULAR)
end)

