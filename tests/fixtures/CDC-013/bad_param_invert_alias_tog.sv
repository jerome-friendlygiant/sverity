// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Combinational self-rewrite pol = stuck_q; if (INVERT) pol = ~pol; keeps pol live when INVERT=1.
// expect: CDC-013 WARNING
// expect: CDC-014 WARNING
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
