// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An unreset guarded-only constant flop is not unconditionally constant and cannot exempt a downstream toggle.
// expect: CDC-013 WARNING
// expect: CDC-014 WARNING
module top(input logic clka, clkb, rst_a, en, output logic q);
  logic q_g, tog_q, s1, s2;
  always_ff @(posedge clka) begin
    if (en) q_g <= 1'b0;
  end
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) tog_q <= 1'b0;
    else tog_q <= tog_q ^ q_g;
  end
  always_ff @(posedge clkb or posedge rst_a) begin
    if (rst_a) {s2, s1} <= '0;
    else {s2, s1} <= {s1, tog_q};
  end
  assign q = s2;
endmodule
