// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The synchronized control is data to a destination register and does not drive any async reset pin.
// expect-none: RDC-024
module top(input logic clka, clkb, rst_n, req, d, output logic q);
  logic r_a, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r_a <= 1'b0; else r_a <= req;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= 2'b0; else {s2, s1} <= {s1, r_a};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d & s2;
endmodule
