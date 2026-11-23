// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-009
module top(input logic clka, clkb, a, b, output logic q);
  logic a_q, b_q, s1;
  always_ff @(posedge clka) begin
    a_q <= a;
    b_q <= b;
  end
  always_ff @(posedge clkb) begin
    s1 <= a_q & b_q;
    q <= s1;
  end
endmodule
