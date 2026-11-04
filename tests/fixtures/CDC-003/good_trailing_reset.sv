// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-003 INFO
// expect-none: CDC-003 ERROR
// expect-none: CDC-028 ERROR
// revert: b2 <= b1; => b2 <= b1 ^ d;
// revert-expect-none: CDC-003 INFO
module top(input logic clka, clkb, rst_b, d, output logic o);
  logic q_a, b1, b2;
  always_ff @(posedge clka) q_a <= d;
  always_ff @(posedge clkb) begin
    b1 <= q_a; b2 <= b1;
    if (rst_b) begin b1 <= 0; b2 <= 0; end
  end
  assign o = b2;
endmodule
