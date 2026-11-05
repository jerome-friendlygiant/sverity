// sverity-args: --top top
// Three sources under one foreign reset feed one register: one cause, one finding, each source kept as evidence.
// expect-count: RDC-004 WARNING 1
module top(input logic clk, ra, rb, a, b, c, output logic q);
  logic s0, s1, s2;
  always_ff @(posedge clk or negedge ra) if (!ra) {s2, s1, s0} <= '0; else {s2, s1, s0} <= {a, b, c};
  always_ff @(posedge clk or negedge rb) if (!rb) q <= 1'b0; else q <= s0 ^ s1 ^ s2;
endmodule
