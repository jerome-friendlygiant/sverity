// sverity-args: --top top
// The wrap-safe elapsed-time test: an unsigned difference is modular on purpose, and no wider context would recover a negative one.
// expect-none: LNT-036
module top(input logic [15:0] now, start, timeout, output logic expired);
  assign expired = (now - start) >= timeout;
endmodule
