# Initial radio reference configurations

Reference configurations are editable starting points. They never guess a COM
port, open a device during discovery, arm transmission, or replace the radio and
cable manuals.

## Yaesu FT-450D

- Hamlib model: `1046` (`RIG_MODEL_FT450` in the current supported-radio list).
- OmniRig command description: `FT-450`.
- Initial CAT framing: 4800 baud, 8 data bits, no parity, 1 stop bit.
- Initial RTS mode: hardware handshake, matching the radio's enabled CAT RTS
  default. The operator may customize every parameter to match the radio menu.
- Use a straight RS-232 connection for the rear CAT interface, not a null-modem
  cable.

The radio's CAT connector gives RTS another function in DATA operation. CW
Assistant therefore treats CAT and direct key/PTT as separate configured
interfaces in the first hardware milestone. Never attach the direct-keying
adapter to an assumed connector or line.

## Yaesu FT-818 / FT-818ND

- Hamlib model: `1041` (`RIG_MODEL_FT818` in the current supported-radio list).
- OmniRig command description: `FT-817`, whose five-byte CAT command set is
  compatible with the FT-818 command protocol.
- Initial CAT framing: 4800 baud, 8 data bits, no parity, 2 stop bits.
- Initial RTS mode: no flow control. All parameters remain customizable.
- The CAT connection requires the appropriate Yaesu cable or an electrically
  compatible level-converting interface; it is not a direct PC RS-232 input.

## Direct COM keying

The starting mapping is RTS for PTT and DTR for KEY on an independently selected
serial interface. Polarity is adapter-dependent and is always configurable; the
initial high-active value is not permission to connect hardware or transmit.

The first safe hardware procedure will require a disconnected-radio line-level
test, then a dummy-load test at minimum power. The application must initialize
both lines inactive, remain disarmed after every open/reconnect, enforce maximum
key-down time, and provide emergency release before any on-air test.

Official references:

- [FT-450D CAT Operation Reference Book](https://yaesu.com/Files/4CB893D7-1018-01AF-FA97E9E9AD48B50C/FT-450D_CAT_OM_ENG_1710-B.pdf)
- [FT-450D product and manuals](https://www.yaesu.com/product-detail.aspx?CatName=Legacy&Model=FT-450D)
- [FT-818 product and manuals](https://yaesu.com/product-detail.aspx?CatName=Legacy&Model=FT-818)
- [Hamlib supported radios](https://github.com/Hamlib/Hamlib/wiki/Supported-Radios)
- [OmniRig engine and command-description documentation](https://www.dxatlas.com/OmniRig/)
