// sverity-args: --top top -G INVERT=0 --sdc @FIXTURES@/_common/async.sdc
// With INVERT=0, dead branch if (INVERT) pol = ~pol; leaves pol aliased to stuck_q and tog_q stuck.
// expect-none: CDC-013
// expect-none: CDC-014
module top #(parameter bit INVERT = 1'b1) (input logic clka, clkb, rst_a, output logic q);
  logic stuck_q, pol, tog_q, s1, s2;
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) stuck_q <= 1'b0;
    else stuck_q <= 1'b0;
  end
  always_comb begin
    pol = stuck_q;
    if (INVERT) pol = ~pol;
  end
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) tog_q <= 1'b0;
    else tog_q <= tog_q ^ pol;
  end
  always_ff @(posedge clkb or posedge rst_a) begin
    if (rst_a) {s2, s1} <= '0;
    else {s2, s1} <= {s1, tog_q};
  end
  assign q = s2;
endmodule
