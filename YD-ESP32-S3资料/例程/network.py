def do_connect():
    import network
    print('connecting to network...')
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if not wlan.isconnected():
        print('connecting to network...')
        wlan.connect('TP-LINK_D68D', '12345678')
        while not wlan.isconnected():
            pass
    print('network config:', wlan.ifconfig())
    
do_connect()