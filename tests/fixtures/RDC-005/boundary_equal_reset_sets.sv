// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Edge of the subset exemption: both domains use the same reset port (rs equals rd), so the obligation is exempt with no synchroniser.
// expect-none: RDC-005
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic s, t;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) s <= 1'b0; else s <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) t <= 1'b0; else t <= s;
  assign q = t;
endmodule
