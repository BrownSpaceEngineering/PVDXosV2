#ifndef MCP23017_REGS_H
#define MCP23017_REGS_H

// MCP23017 registers with IOCON.BANK = 0 (power-on default), A/B pairs are adjacent (MCP23017 pg. 16)
#define MCP23017_IODIRA 0x00U /* R/W  I/O direction, 1 = input (POR 0xFF) */
#define MCP23017_IODIRB 0x01U
#define MCP23017_IPOLA 0x02U /* R/W  Input polarity */
#define MCP23017_IPOLB 0x03U
#define MCP23017_GPINTENA 0x04U /* R/W  Interrupt-on-change enable */
#define MCP23017_GPINTENB 0x05U
#define MCP23017_DEFVALA 0x06U /* R/W  Interrupt default compare value */
#define MCP23017_DEFVALB 0x07U
#define MCP23017_INTCONA 0x08U /* R/W  Interrupt control */
#define MCP23017_INTCONB 0x09U
#define MCP23017_IOCON 0x0AU /* R/W  Configuration (also mirrored at 0x0B) */
#define MCP23017_GPPUA 0x0CU /* R/W  Pull-up enable */
#define MCP23017_GPPUB 0x0DU
#define MCP23017_INTFA 0x0EU /* R    Interrupt flags */
#define MCP23017_INTFB 0x0FU
#define MCP23017_INTCAPA 0x10U /* R    Interrupt capture */
#define MCP23017_INTCAPB 0x11U
#define MCP23017_GPIOA 0x12U /* R/W  Port value (reads pin levels) */
#define MCP23017_GPIOB 0x13U
#define MCP23017_OLATA 0x14U /* R/W  Output latch */
#define MCP23017_OLATB 0x15U

// IOCON fields (MCP23017 pg. 20)
#define MCP23017_IOCON_BANK (1U << 7)   /* 1 = registers split by port */
#define MCP23017_IOCON_MIRROR (1U << 6) /* 1 = INTA/INTB internally connected */
#define MCP23017_IOCON_SEQOP (1U << 5)  /* 1 = address pointer does not increment */
#define MCP23017_IOCON_DISSLW (1U << 4) /* 1 = SDA slew rate control disabled */
#define MCP23017_IOCON_HAEN (1U << 3)   /* MCP23S17 only */
#define MCP23017_IOCON_ODR (1U << 2)    /* 1 = INT pins open-drain */
#define MCP23017_IOCON_INTPOL (1U << 1) /* 1 = INT pins active-high */

#endif // MCP23017_REGS_H
