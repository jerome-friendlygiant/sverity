// sverity-args: --top top
// The same 16 bits seen as two packed structs of plain bit fields: synthesis builds the wires as written. INFO, not a hazard.
// expect: LNT-005 INFO 8
// expect-none: LNT-005 WARNING
module top(input logic [15:0] raw, output logic y);
  typedef struct packed {logic [3:0] hi; logic [3:0] lo;} pair_t;
  pair_t [1:0] pairs;
  assign pairs = raw;
  assign y = pairs[0].hi[0] ^ pairs[1].lo[0];
endmodule
