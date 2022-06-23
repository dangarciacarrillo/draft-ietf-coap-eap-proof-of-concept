# CoAP-EAP proof-of-concept


This proof-of-concept implementation contains the following components:


- CoAP-EAP IoT device // EAP peer
- CoAP-EAP Controller // EAP Authenticator 
- FreeRADIUS serve    // EAP server

## Features
- CoAP-EAP Controller implemented in C/C++
- CoAP-EAP IoT device implementation in Contiki and tested in Cooja
- FreeRADIUS implementation with EAP-PSK support

The proof-of-concept was succesfully tested in UBUNTU 12.04. The script found in "setup scripts/coap-eap.sh" contains all the information and the commands needed to setup the PoC.


## License
MIT
Third party software used in this repository might have other licenses.
