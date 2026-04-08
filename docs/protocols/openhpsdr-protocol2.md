# OpenHPSDR Protocol 2

## Status: Reference placeholder

## Overview

Protocol 2 is the newer OpenHPSDR protocol used by Orion MkII, Saturn (ANAN-G2),
and newer boards. It uses TCP for commands and UDP for high-bandwidth data.

## Key Characteristics

- **Command channel:** TCP
- **Data channel:** UDP with separate streams per data type
- **Discovery:** UDP broadcast/multicast on port 1024
- **Independent receiver streams:** Each receiver has its own data stream
- **Wider bandwidth support:** Higher sample rates possible

## Advantages over Protocol 1

- Reliable command delivery (TCP)
- Independent receiver data streams (not multiplexed)
- Better scalability for multiple receivers
- Structured command/response protocol

## Official Specification

See: https://openhpsdr.org/wiki/index.php?title=Protocol_2
