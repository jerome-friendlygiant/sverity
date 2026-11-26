// sverity-args: --top top
// Bit 1 of v is reset to an X literal, and the primary output reads that bit through v: whether the output is held or reset in
// reset is not decided, so RDC-025 is Unresolved (not an unreset output, and not a reset one).
// expect-unresolved: RDC-025
module top(input logic clka, rst_n, d, output logic q, output wire held);
  logic [1:0] v;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) v <= 2'bx0; else v <= {d, d};
  assign held = v[1];
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 0; else q <= d;
endmodule
