// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// One capture has depth 1 and its sibling has depth 2, the smallest unequal-depth case.
// expect: CDC-032 ERROR
module top(input logic clka, clkb, d, output logic short_q, long_q);
  logic src_q, short_stage, long_s1, long_s2;
  always_ff @(posedge clka) src_q <= d;
  always_ff @(posedge clkb) begin
    short_stage <= src_q;
    long_s1 <= src_q;
    long_s2 <= long_s1;
  end
  assign short_q = short_stage;
  assign long_q = long_s2;
endmodule
