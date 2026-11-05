// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_unequal_depth.sv: bit 1 has the same two stages as bit 0 (its third stage is removed), so both bits have depth 2.
// expect-none: CDC-032
module top(input logic clka, clkb, rst_n, input logic [1:0] d, output logic [1:0] q);
  logic [1:0] a;
  logic b0s1, b0s2, b1s1, b1s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a <= '0; else a <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {b0s2, b0s1, b1s2, b1s1} <= '0; else {b0s2, b0s1, b1s2, b1s1} <= {b0s1, a[0], b1s1, a[1]};
  assign q = {b1s2, b0s2};
endmodule
