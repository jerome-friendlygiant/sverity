// sverity-args: --top top
// Bit 1 of v is reset to an X literal: an X is not a binary reset, so whether v is partly reset is Unresolved (RDC-020),
// not established as held or as reset.
// expect-unresolved: RDC-020
module top(input logic clka, rst_n, d, output logic q, output wire held);
  logic [1:0] v;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) v <= 2'bx0; else v <= {d, d};
  assign held = v[1];
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 0; else q <= v[1];
endmodule
