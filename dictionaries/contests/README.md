# Contest exchange profiles
#
# One file per contest. Each describes only the exchange: what the other
# station sends, what this station sends, and the order of each. Everything
# else -- the conversation flow, its states, and every transmit safety gate --
# is built by the application and cannot be named here. In particular no file
# in this directory can arm a transmitter, change a key-down timeout, or
# relax callsign confirmation.
#
# Format. Lines beginning with # are comments; blank lines are ignored.
# The header is key = value, then sections in square brackets.
#
#   id           the profile's stable identifier
#   title        what an operator sees
#   revision     bump when the exchange changes
#   rules-url    the sponsor's published rules
#   valid-from   optional ISO date the exchange takes effect
#   valid-to     optional ISO date it stops applying
#
# [received] and [station] hold one field per line:
#
#   <id> <kind> <required|optional> [max=N] [values=...] [alias=A>B] [cut=A>B]
#
#   kind        rst, serial, enumeration, callsign, or text
#   max         maximum characters, default 64
#   values      an inline list (Q,A,B) or a generated set (cq-zones)
#   alias       A is read as B, whole token: alias=5NN>599
#   cut         A is read as B, character by character: cut=T>0
#
# [runner] and [caller] give the order each side sends, one token per line:
#
#   received:<field>   a field the other station sent
#   station:<field>    a field this station sends
#   literal:<text>     fixed text
