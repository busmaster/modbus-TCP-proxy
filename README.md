# modbus-TCP-proxy

This proxy is used to access a ModbusTCP server that just one connect at a time, e.g. Marstek Venus E 3.0.
The proxy keeps open one connection to the server and exectutes cleints requests sequentially.

## Usage
```
./modbus-proxy <listen-port> <server-ip> <server-port>
<listen-port>: The port that the clients have to use instead of the standard TCP of the master. Select a port number, e.g. 5020
<server-ip>: The IP address of the server
<server-port>: TCP Modbus port of the server, typically 502
```

## Debian service install example
1) edit modbus-prox-pv.service according your environment/requirements
2) sudo cp modbus-proxy-pv.service /etc/systemd/system/
3) sudo systemctl start modbus-proxy-pv.service
4) sudo systemctl enable modbus-proxy.pv.service
