// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --option CDC-005.min_stages=3
// The synchroniser checks apply to a primary input as to a register: a tap on its middle stage is reported.
// expect: CDC-007 WARNING
module top(input logic clka, clkb, d, output logic q, tap);
  logic s1, s2, s3;
  logic unused_q;
  always_ff @(posedge clka) unused_q <= d;
  always_ff @(posedge clkb) begin
    s1 <= d;
    s2 <= s1;
    s3 <= s2;
  end
  assign q = s3;
  assign tap = ~s2;
endmodule
