// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// s2 has been through the two required stages, so it is an output: a pipeline register after it and logic beside it are ordinary use.
// expect-none: CDC-007
module top(input logic clka, clkb, d, output logic q, tap);
  logic a_q, s1, s2, s3;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin
    s1 <= a_q;
    s2 <= s1;
    s3 <= s2;
  end
  assign q = s3;
  assign tap = ~s2;
endmodule
