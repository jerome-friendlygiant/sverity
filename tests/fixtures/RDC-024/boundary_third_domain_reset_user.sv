// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Control synchronised from clka into clkb then used as async reset in clka must still emit RDC-024 WARNING.
// expect: RDC-024 WARNING
module top(input logic clka, clkb, rst_n, req, d, output logic q);
  logic r_a, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r_a <= 1'b1; else r_a <= req;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= 2'b11; else {s2, s1} <= {s1, r_a};
  always_ff @(posedge clka or negedge s2) if (!s2) q <= 1'b0; else q <= d;
endmodule
