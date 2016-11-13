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


wifi.setmode(wifi.SOFTAP)
function es_ok()
	print(string.format("Connected to wifi ssid: %s our IP: %s", wifi.sta.getconfig(), wifi.sta.getip()))
end
function es_bad(err, str)
	print("es failed, err: ", err, " str: ", str)
end
function es_dbg(str)
	print("es dbg: ", str)
end
enduser_setup.start(es_ok, es_bad, es_dbg)
enduser_setup.start(es_ok, es_bad)
enduser_setup.stop()
